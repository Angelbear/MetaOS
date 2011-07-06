#define UNKNOWN_ERROR -3
#define CA_FALSE -1
#define CA_FORMAT_ERROR -2
#define CA_TRUE 1
#define CA_SUCCESS 0

#define CA_SERVER 210.75.5.231
#define CA_PORT 8080
#define CHECK_REGISTER_URL_TEP  "http://210.75.5.231:8080/authcloud/checkUserForCPK?IMSI=%s"
#define APPLY_RANDOM_URL_TEP    "http://210.75.5.231:8080/authcloud/RequestRandom?UID=%s"
#define APPLY_CPK_PRI_URL_TEP   "http://210.75.5.231:8080/authcloud/reqCPKPriKey?IMSI=%s"
#define CPK_AUTHENTICATION_URL_TEP  "http://210.75.5.231:8080/authcloud/verifySignForCPK?UID=%s&signature=%s"

int check_register(char* imsi, char* pri, char* uid);
int apply_random(char* uid, char* random);
int apply_cpk_pri(char* imsi, char* pri, char* uid);
int cpk_authentication(char* uid, char*signature);

int is_priKey_exist(char* imsi,char* path,char* uid);
int save_priKey(char* priKey, char* filename);
