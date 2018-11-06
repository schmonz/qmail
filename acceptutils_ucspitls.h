#define UCSPITLS_UNAVAILABLE 0
#define UCSPITLS_AVAILABLE   1
#define UCSPITLS_REQUIRED    2

extern int ucspitls_level(void);
extern int tls_init(void);
extern int tls_info(void (*)());
