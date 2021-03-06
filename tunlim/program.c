#include "program.h"

#include <string.h>
#include <sys/mman.h> 
#include <sys/stat.h> 
#include <sys/types.h> 
#include <unistd.h>

#include "crc64.h"
#include "log.h"
#include "thread_shared.h" 
#include "lmdb.h"
#include "cache_domains.h"

#define E(expr) CHECK((rc = (expr)) == MDB_SUCCESS, #expr)
#define RES(err, expr) ((rc = expr) == (err) || (CHECK(!rc, #expr), 0))
#define CHECK(test, msg) ((test) ? (void)0 : ((void)debugLog("%s:%d: %s: %s\n", __FILE__, __LINE__, msg, mdb_strerror(rc)), abort()))

int loop = 1;
MDB_env *mdb_env = 0;
cache_domain *whitelist = 0;

int create(void **args)
{
	MDB_dbi dbi;
	MDB_txn *txn = 0;
	int rc = 0;
	//int fd = shm_open(C_MOD_MUTEX, O_CREAT | O_TRUNC | O_RDWR, 0600);
	//if (fd == -1)
	//	return fd;

	//E(ftruncate(fd, sizeof(struct shared)));

	//thread_shared = (struct shared*)mmap(0, sizeof(struct shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	//if (thread_shared == NULL)
	//	return -1;
	//thread_shared->sharedResource = 0;
	
	E(mdb_env_create(&mdb_env));
	E(mdb_env_set_maxreaders(mdb_env, 16));
	E(mdb_env_set_maxdbs(mdb_env, 4));
	size_t max = 1073741824;
	E(mdb_env_set_mapsize(mdb_env, max)); //1GB
	E(mdb_env_open(mdb_env, "/var/whalebone/tunlim", /*MDB_FIXEDMAP | MDB_NOSYNC*/ 0, 0664));

	E(mdb_txn_begin(mdb_env, 0, 0, &txn));
	E(mdb_dbi_open(txn, "cache", MDB_CREATE, &dbi));
	E(mdb_txn_commit(txn));
	mdb_close(mdb_env, dbi);

	init();

	pthread_t thr_id;
	loop = 1;
	E(pthread_create(&thr_id, NULL, &threadproc, NULL));

	*args = (void *)thr_id;

	debugLog("\"%s\":\"%s\"", "message", "created");

	return 0;
}

int destroy(void *args)
{
	int rc = 0;
	loop = 0;

	mdb_env_close(mdb_env);
	mdb_env = NULL;

	void *res = NULL;
	pthread_t thr_id = (pthread_t)args;
	E(pthread_join(thr_id, res));

	debugLog("\"%s\":\"%s\"", "message", "successfully destroyed");

	return 0;
}

int init()
{
	FILE * fp = 0;
	fp = fopen("crc64.dat", "rb");
	if (!fp)
	{
		debugLog("\"error\":\"unable to open .dat file\"");
		return -1;
	}

	fseek(fp, 0L, SEEK_END);
	long sz = ftell(fp);
	long numelem = sz / 8;

	fseek(fp, 0L, SEEK_SET);

	int read_result;
	unsigned long long *buffer = (unsigned long long *)calloc(numelem, sizeof(unsigned long long));
	char buf[8];
	int i = 0;
	while ((read_result = fread(buf, sizeof(unsigned long long), 1, fp)) > 0)
	{
		memcpy(buffer + (i++), buf, sizeof(unsigned long long));
	}

	whitelist = cache_domain_init_ex2(buffer, numelem);

	return 0;
}

void* threadproc(void *arg)
{
	debugLog("\"%s\":\"%s\"", "message", "threadproc");
	while (loop)
	{
		//debugprint();

		sleep(5);
	}

	return NULL;
}

int increment(const char *address, const char *domainl)
{
	MDB_dbi dbi;
	MDB_val key, data;
	MDB_txn *txn = 0;
	int rc = 0;
	char bkey[8] = { 0 }; 
	char value[2400] = { 0 };
	
	char combokey[2048] = { 0 };
	sprintf((char *)&combokey, "%s:%s", address, domainl);

	unsigned long long crc = crc64(0, combokey, strlen(combokey));
	memcpy(&bkey, &crc, 8);

	//Get data, if any
	E(mdb_txn_begin(mdb_env, 0, 0, &txn));
	if ((rc = mdb_dbi_open(txn, "cache", 0, &dbi)) == 0)
	{
		key.mv_size = sizeof(unsigned long long);
		key.mv_data = (void *)bkey;

		if ((rc = mdb_get(txn, dbi, &key, &data)) == 0)
		{
			memcpy(&value, data.mv_data, data.mv_size);
		}
	}
	mdb_txn_abort(txn);
	txn = 0;
	mdb_close(mdb_env, dbi);

	//Modify data
	double entr = calc_entropy(domainl);
	if (strlen(value) == 0)
	{
		sprintf((char *)&value, "%s:%s:0:%lf\0", address, domainl, entr);
	}

	char delim[] = ":";
	char *ptr = strtok(value, delim);
	ptr = strtok(NULL, delim);
	if (ptr)
	{
		ptr = strtok(NULL, delim);
		if (ptr)
		{
			int num = atoi(ptr);
			num++;
			sprintf((char *)&value, "%s:%s:%d:%lf\0", address, domainl, num, entr);
		}
	}

	//Update data
	txn = 0;
	E(mdb_txn_begin(mdb_env, 0, 0, &txn)); 
	E(mdb_dbi_open(txn, "cache", 0, &dbi));
	key.mv_size = sizeof(unsigned long long);
	key.mv_data = (void *)bkey;
	data.mv_size = strlen(value);
	data.mv_data = (void *)value;
	E(mdb_put(txn, dbi, &key, &data, 0));

	E(mdb_txn_commit(txn));
	mdb_close(mdb_env, dbi);

	return 0;
}

int search(const char * domainToFind, struct ip_addr * userIpAddress, const char * userIpAddressString, int rrtype, char * originaldomain, char * logmessage)
{
	unsigned long long crc = crc64(0, (const char*)domainToFind, strlen(domainToFind));
	debugLog("\"type\":\"search\",\"message\":\"ioc '%s' crc'%llx'\"", domainToFind, crc);

	increment(userIpAddressString, domainToFind);

	//domain domain_item = {};
	//if (cache_domain_contains(whitelist, crc, &domain_item, 0) == 1)
	//{
	//	debugLog("\"type\":\"search\",\"message\":\"detected ioc '%s'\"", domainToFind);
	//}
	//else
	//{
	//	debugLog("\"type\":\"search\",\"message\":\"cache domains does not have a match to '%s'\"", domainToFind);
	//}

	return 0;
}

int explode(char * domainToFind, struct ip_addr * userIpAddress, const char * userIpAddressString, int rrtype)
{
	char message[2048] = { 0 };
	char logmessage[2048] = { 0 };
	char *ptr = domainToFind;
	ptr += strlen(domainToFind);
	int result = 0;
	int found = 0;
	while (ptr-- != (char *)domainToFind)
	{
		if (ptr[0] == '.')
		{
			if (++found > 1)
			{
				sprintf(message, "\"type\":\"explode\",\"message\":\"search %s\"", ptr + 1);
				debugLog(message);
				if ((result = search(ptr + 1, userIpAddress, userIpAddressString, rrtype, domainToFind, logmessage)) != 0)
				{
					if (logmessage[0] != '\0')
					{
						fileLog(logmessage);
					}
					return result;
				}
			}
		}
		else
		{
			if (ptr == (char *)domainToFind)
			{
				sprintf(message, "\"type\":\"explode\",\"message\":\"search %s\"", ptr);
				debugLog(message);
				if ((result = search(ptr, userIpAddress, userIpAddressString, rrtype, domainToFind, logmessage)) != 0)
				{
					if (logmessage[0] != '\0')
					{
						fileLog(logmessage);
					}
					return result;
				}
			}
		}
	}
	if (logmessage[0] != '\0')
	{
		fileLog(logmessage);
	}

	return 0;
}

void print()
{
	MDB_val key, data;
	MDB_dbi dbi;
	MDB_txn *txn = 0;
	MDB_cursor *cursor = 0;
	int rc = 0;

	E(mdb_txn_begin(mdb_env, 0, 0, &txn));
	if ((rc = mdb_dbi_open(txn, "cache", 0, &dbi)) == 0)
	{
		E(mdb_cursor_open(txn, dbi, &cursor));

		while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
			debugLog("\"key\":\"%llx\", \"data\":\"%s\"", *(unsigned long long*)key.mv_data, data.mv_data);
		}
		mdb_cursor_close(cursor);

	}
	mdb_close(mdb_env, dbi);
	mdb_txn_abort(txn);
}

static void prstat(MDB_stat *ms)
{
	debugLog("  Page size: %u", ms->ms_psize);
	debugLog("  Tree depth: %u", ms->ms_depth);
	debugLog("  Branch pages: %u", ms->ms_branch_pages);
	debugLog("  Leaf pages: %u", ms->ms_leaf_pages);
	debugLog("  Overflow pages: %u", ms->ms_overflow_pages);
	debugLog("  Entries: %u", ms->ms_entries);
}

void stats()
{
	int rc = 0;
	MDB_stat stat;
	E(mdb_env_stat(mdb_env, &stat));
	prstat(&stat);

	MDB_dbi dbi;
	MDB_txn *txn = 0;
	E(mdb_txn_begin(mdb_env, 0, 0, &txn));
	E(mdb_dbi_open(txn, "cache", 0, &dbi));
	E(mdb_stat(txn, dbi, &stat));
	prstat(&stat);

	mdb_close(mdb_env, dbi);
	mdb_txn_abort(txn);
}

int makehist(char *S, int *hist, int len) 
{
	int wherechar[256];
	int i, histlen;
	histlen = 0;
	for (i = 0; i < 256; i++)wherechar[i] = -1;
	for (i = 0; i < len; i++) {
		if (wherechar[(int)S[i]] == -1) {
			wherechar[(int)S[i]] = histlen;
			histlen++;
		}
		hist[wherechar[(int)S[i]]]++;
	}
	return histlen;
}

double entropy(int *hist, int histlen, int len)
{
	int i;
	double H;
	H = 0;
	for (i = 0; i < histlen; i++) {
		H -= (double)hist[i] / len * log2((double)hist[i] / len);
	}
	return H;
}

double calc_entropy(const char * string)
{
	int len, *hist, histlen;
	len = strlen(string);
	hist = (int*)calloc(len, sizeof(int));
	histlen = makehist(string, hist, len);

	double hv = entropy(hist, histlen, len);
	free(hist);
	return hv;

}

#ifdef NOKRES 

static int usage()
{
	fprintf(stdout, "Available commands: ");
	fprintf(stdout, "\n");
	fprintf(stdout, "exit\n");
	fprintf(stdout, "set\n");
	fprintf(stdout, "load\n");
	fprintf(stdout, "stats\n");
	fprintf(stdout, "print\n");
	fprintf(stdout, "hist\n");
	return 0;
}

static int set()
{
	int err = 0;
	char command[80] = { 0 };
	fprintf(stdout, "\nEnter ip address: ");
	scanf("%79s", command);
	char ip[80] = { 0 };
	strcpy(ip, command);
	fprintf(stdout, "\nEnter value: ");
	scanf("%79s", command);

	return increment(ip, command);
}


static int load()
{
	return init();
}

static int hist()
{
	int err = 0;
	char command[80] = { 0 };
	fprintf(stdout, "\nEnter string: ");
	scanf("%79s", command);

	debugLog("%lf", calc_entropy(command));

	return 0;
}

static int userInput()
{
	char command[80] = { 0 };
	fprintf(stdout, "\nType command:");
	scanf("%79s", command);

	if (strcmp("exit", command) == 0)
		return 0;
	else if (strcmp("set", command) == 0)
		set();
	else if (strcmp("load", command) == 0)
		load();
	else if (strcmp("print", command) == 0)
		print();
	else if (strcmp("stats", command) == 0)
		stats();
	else if (strcmp("hist", command) == 0)
		hist();
	else
		usage();

	return 1;
}

int main()
{
	int err = 0;
	int thr_id = 0;
	if ((err = create((void *)&thr_id)) != 0)
	{
		debugLog("\"%s\":\"%s\"", "message", "error in create");
		return err;
	}

	usage();
	while (userInput());

	if ((err = destroy((void *)&thr_id)) != 0)
	{
		debugLog("\"%s\":\"%s\"", "message", "error in destroy");
		return err;
	}

	return err;
}

#endif