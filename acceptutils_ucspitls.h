#define UCSPITLS_UNAVAILABLE 0
#define UCSPITLS_AVAILABLE   1
#define UCSPITLS_REQUIRED    2

int ucspitls_level(void);
int tls_init(void);
int tls_info(void (*)(const char *,const char *));
