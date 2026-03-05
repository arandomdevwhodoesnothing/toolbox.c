/*
 * tests/test_all.c
 * Test suite for our freestanding libc.
 * No system libc — uses only our own implementations.
 */
#include "libtool.h"

/* ── Test framework ──────────────────────────────────────────────── */
static int g_pass = 0, g_fail = 0;

static void __test_check(int ok, const char *expr, const char *file, int line) {
    if (ok) { g_pass++; }
    else {
        g_fail++;
        fprintf(stderr, "  FAIL: %s  [%s:%d]\n", expr, file, line);
    }
}

#define CHECK(c)        __test_check(!!(c), #c, __FILE__, __LINE__)
#define CHECK_EQ(a,b)   __test_check((a)==(b), #a "==" #b, __FILE__, __LINE__)
#define CHECK_STR(a,b)  __test_check(strcmp((a),(b))==0, #a "==" #b, __FILE__, __LINE__)
#define CHECK_NULL(p)   __test_check((p)==NULL, #p "==NULL", __FILE__, __LINE__)
#define CHECK_NN(p)     __test_check((p)!=NULL, #p "!=NULL", __FILE__, __LINE__)

#define SUITE(name) printf("\n[%s]\n", name)
#define TEST(name)  printf("  %-36s", name)
#define PASS()      printf("ok\n")

/* ── Memory ──────────────────────────────────────────────────────── */
static void test_memset(void) {
    TEST("memset");
    char buf[64];
    memset(buf, 0xAB, 64);
    int ok=1; for(int i=0;i<64;i++) if((unsigned char)buf[i]!=0xAB)ok=0;
    CHECK(ok);
    memset(buf,0,64);
    ok=1; for(int i=0;i<64;i++) if(buf[i])ok=0;
    CHECK(ok);
    PASS();
}

static void test_memcpy(void) {
    TEST("memcpy");
    char s[32],d[32];
    for(int i=0;i<32;i++) s[i]=(char)i;
    memcpy(d,s,32);
    CHECK(memcmp(d,s,32)==0);
    PASS();
}

static void test_memmove(void) {
    TEST("memmove");
    char buf[32]="Hello World!";
    memmove(buf+3,buf,12);
    CHECK(memcmp(buf+3,"Hello World!",12)==0);
    char b2[16]="ABCDEFGH";
    memmove(b2,b2+2,6);
    CHECK_STR(b2,"CDEFGHGH");
    PASS();
}

static void test_malloc(void) {
    TEST("malloc/free/calloc/realloc");
    /* malloc */
    void *p=malloc(256); CHECK_NN(p);
    memset(p,0x42,256); free(p);
    /* calloc */
    int *a=(int*)calloc(10,sizeof(int)); CHECK_NN(a);
    int z=1; for(int i=0;i<10;i++) if(a[i])z=0; CHECK(z);
    free(a);
    /* realloc */
    char *s=(char*)malloc(8); CHECK_NN(s);
    strlcpy(s,"hello",8);
    s=(char*)realloc(s,64); CHECK_NN(s);
    CHECK_STR(s,"hello");
    free(s);
    /* many allocs */
    void *ptrs[256];
    for(int i=0;i<256;i++) ptrs[i]=malloc((size_t)(i+1)*8);
    for(int i=0;i<256;i++) free(ptrs[i]);
    PASS();
}

/* ── Strings ─────────────────────────────────────────────────────── */
static void test_strlen(void) {
    TEST("strlen/strnlen");
    CHECK_EQ(strlen(""), (size_t)0);
    CHECK_EQ(strlen("hello"), (size_t)5);
    char big[1024]; memset(big,'X',1023); big[1023]='\0';
    CHECK_EQ(strlen(big),(size_t)1023);
    CHECK_EQ(strnlen("hello",3),(size_t)3);
    PASS();
}

static void test_strcmp(void) {
    TEST("strcmp/strncmp/strcasecmp");
    CHECK_EQ(strcmp("abc","abc"),0);
    CHECK(strcmp("abc","abd")<0);
    CHECK(strcmp("abd","abc")>0);
    CHECK_EQ(strcasecmp("Hello","HELLO"),0);
    CHECK_EQ(strncmp("abcX","abcY",3),0);
    PASS();
}

static void test_strchr(void) {
    TEST("strchr/strrchr/strstr");
    CHECK_NN(strchr("hello",'e'));
    CHECK_NULL(strchr("hello",'z'));
    CHECK_NN(strstr("hello world","world"));
    CHECK_NULL(strstr("hello","xyz"));
    const char *s="abcabc";
    CHECK(strrchr(s,'a')==s+3);
    PASS();
}

static void test_strdup(void) {
    TEST("strdup/strndup");
    char *d=strdup("hello"); CHECK_STR(d,"hello"); free(d);
    d=strndup("hello world",5); CHECK_STR(d,"hello"); free(d);
    PASS();
}

static void test_strtok(void) {
    TEST("strtok_r");
    char s[]="one two three";
    char *save, *tok;
    tok=strtok_r(s," ",&save); CHECK_STR(tok,"one");
    tok=strtok_r(NULL," ",&save); CHECK_STR(tok,"two");
    tok=strtok_r(NULL," ",&save); CHECK_STR(tok,"three");
    tok=strtok_r(NULL," ",&save); CHECK_NULL(tok);
    PASS();
}

/* ── Numbers ─────────────────────────────────────────────────────── */
static void test_atoi(void) {
    TEST("atoi/strtol/strtoul");
    CHECK_EQ(atoi("42"),42);
    CHECK_EQ(atoi("-99"),-99);
    CHECK_EQ((long)strtol("0xFF",NULL,16),(long)255);
    CHECK_EQ((long)strtol("0777",NULL,0),(long)511);
    CHECK_EQ((unsigned long)strtoul("4294967295",NULL,10),(unsigned long)4294967295UL);
    PASS();
}

static void test_atof(void) {
    TEST("atof/strtod");
    double v=atof("3.14"); CHECK(v>3.13&&v<3.15);
    v=atof("-2.718"); CHECK(v>-2.72&&v<-2.71);
    v=strtod("1e3",NULL); CHECK(v>999&&v<1001);
    PASS();
}

/* ── Math ────────────────────────────────────────────────────────── */
static void test_math(void) {
    TEST("floor/ceil/round/fmod");
    CHECK(floor(2.9)<2.001); CHECK(ceil(2.1)>2.999);
    CHECK(round(2.5)>2.999); CHECK(round(2.4)<2.001);
    CHECK(fabs(fmod(10.0,3.0)-1.0)<0.0001);
    PASS();
}

static void test_sqrt(void) {
    TEST("sqrt");
    double s=sqrt(9.0); CHECK(s>2.999&&s<3.001);
    s=sqrt(2.0); CHECK(s>1.4142&&s<1.4143);
    PASS();
}

static void test_exp_log(void) {
    TEST("exp/log/log2/log10");
    double e=exp(1.0); CHECK(e>2.718&&e<2.719);
    double l=log(M_E);  CHECK(l>0.999&&l<1.001);
    CHECK(log2(1024.0)>9.999&&log2(1024.0)<10.001);
    CHECK(log10(1000.0)>2.999&&log10(1000.0)<3.001);
    PASS();
}

static void test_trig(void) {
    TEST("sin/cos/tan/atan2");
    CHECK(fabs(sin(0.0))<0.001);
    CHECK(fabs(cos(0.0)-1.0)<0.001);
    CHECK(fabs(sin(M_PI/6)-0.5)<0.001);
    CHECK(fabs(cos(M_PI/3)-0.5)<0.001);
    CHECK(fabs(tan(M_PI/4)-1.0)<0.001);
    PASS();
}

static void test_pow(void) {
    TEST("pow");
    CHECK(fabs(pow(2.0,10.0)-1024.0)<0.001);
    CHECK(fabs(pow(3.0,3.0)-27.0)<0.001);
    CHECK(fabs(pow(4.0,0.5)-2.0)<0.001);
    PASS();
}

/* ── Printf ──────────────────────────────────────────────────────── */
static void test_snprintf(void) {
    TEST("snprintf");
    char buf[256];
    snprintf(buf,sizeof(buf),"%d",42);        CHECK_STR(buf,"42");
    snprintf(buf,sizeof(buf),"%05d",42);      CHECK_STR(buf,"00042");
    snprintf(buf,sizeof(buf),"%-8s|","hi");   CHECK_STR(buf,"hi      |");
    snprintf(buf,sizeof(buf),"%x",255);       CHECK_STR(buf,"ff");
    snprintf(buf,sizeof(buf),"%#X",255);      CHECK_STR(buf,"0XFF");
    snprintf(buf,sizeof(buf),"%s","hello");   CHECK_STR(buf,"hello");
    snprintf(buf,sizeof(buf),"%%");           CHECK_STR(buf,"%");
    snprintf(buf,sizeof(buf),"%lld",-9876543210LL); CHECK_STR(buf,"-9876543210");
    /* truncation */
    int r=snprintf(buf,8,"Hello, World!");
    CHECK_EQ(r,13);
    CHECK_STR(buf,"Hello, ");
    PASS();
}

/* ── Sort ────────────────────────────────────────────────────────── */
static int cmp_int(const void *a, const void *b) { return *(int*)a-*(int*)b; }

static void test_qsort(void) {
    TEST("qsort/bsearch");
    int arr[]={9,3,7,1,5,2,8,4,6,0};
    qsort(arr,10,sizeof(int),cmp_int);
    int ok=1; for(int i=1;i<10;i++) if(arr[i]<arr[i-1])ok=0;
    CHECK(ok);
    CHECK_EQ(arr[0],0); CHECK_EQ(arr[9],9);
    int key=7;
    int *found=(int*)bsearch(&key,arr,10,sizeof(int),cmp_int);
    CHECK_NN(found); CHECK_EQ(*found,7);
    key=99;
    found=(int*)bsearch(&key,arr,10,sizeof(int),cmp_int);
    CHECK_NULL(found);
    PASS();
}

/* ── stdio ───────────────────────────────────────────────────────── */
static void test_stdio(void) {
    TEST("fopen/fwrite/fread/fclose");
    FILE *f=fopen("/tmp/__libc_test__","w");
    CHECK_NN(f);
    if(f){
        fprintf(f,"line one\nline two\n");
        fclose(f);
    }
    f=fopen("/tmp/__libc_test__","r");
    CHECK_NN(f);
    if(f){
        char buf[64];
        char *r=fgets(buf,sizeof(buf),f); CHECK_NN(r);
        CHECK_STR(buf,"line one\n");
        r=fgets(buf,sizeof(buf),f); CHECK_NN(r);
        CHECK_STR(buf,"line two\n");
        CHECK(feof(f)||fgetc(f)==EOF);
        fclose(f);
    }
    unlink("/tmp/__libc_test__");
    PASS();
}

/* ── Main ────────────────────────────────────────────────────────── */
int main(void) {
    printf("libc (freestanding) — Test Suite\n");
    printf("==================================\n");

    SUITE("Memory");
    test_memset();
    test_memcpy();
    test_memmove();
    test_malloc();

    SUITE("Strings");
    test_strlen();
    test_strcmp();
    test_strchr();
    test_strdup();
    test_strtok();

    SUITE("Numbers");
    test_atoi();
    test_atof();

    SUITE("Math  (no -lm)");
    test_math();
    test_sqrt();
    test_exp_log();
    test_trig();
    test_pow();

    SUITE("Printf");
    test_snprintf();

    SUITE("Sort");
    test_qsort();

    SUITE("stdio");
    test_stdio();

    printf("\n==================================\n");
    printf("Results: %d passed", g_pass);
    if(g_fail) printf(", %d FAILED", g_fail);
    printf("\n");
    return g_fail ? 1 : 0;
}
