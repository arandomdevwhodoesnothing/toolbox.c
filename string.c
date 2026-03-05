/*
 * libc/src/string.c  — memset/cpy/move/cmp/chr, all str* functions
 * libc/src/math.c    — floor/ceil/sqrt/sin/cos/pow/log
 * libc/src/sort.c    — qsort, bsearch
 * Combined for simplicity.
 */

#include "../include/libtool.h"

/* ══════════════════════════════════════════════════════════════
 * MEMORY
 * ══════════════════════════════════════════════════════════════ */

void *memset(void *dst, int c, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    unsigned char  b = (unsigned char)c;
    size_t i = 0;
    while (i < n && ((uintptr_t)(p+i) & 7)) p[i++] = b;
    if (n - i >= 8) {
        uint64_t w = (uint64_t)b * 0x0101010101010101ULL;
        uint64_t *wp = (uint64_t *)(p+i);
        size_t words = (n-i)/8;
        for (size_t j=0;j<words;j++) wp[j]=w;
        i += words*8;
    }
    while (i < n) p[i++] = b;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i = 0;
    while (i < n && ((uintptr_t)(d+i) & 7)) { d[i]=s[i]; i++; }
    if (n-i >= 8) {
        uint64_t *dp=(uint64_t*)(d+i); const uint64_t *sp=(const uint64_t*)(s+i);
        size_t words=(n-i)/8;
        for(size_t j=0;j<words;j++) dp[j]=sp[j];
        i+=words*8;
    }
    while (i < n) { d[i]=s[i]; i++; }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d=(unsigned char*)dst;
    const unsigned char *s=(const unsigned char*)src;
    if (d==s||!n) return dst;
    if (d<s||d>=s+n) return memcpy(dst,src,n);
    for (size_t i=n;i>0;i--) d[i-1]=s[i-1];
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa=(const unsigned char*)a, *pb=(const unsigned char*)b;
    for (size_t i=0;i<n;i++) if(pa[i]!=pb[i]) return pa[i]-pb[i];
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p=(const unsigned char*)s;
    unsigned char b=(unsigned char)c;
    for(size_t i=0;i<n;i++) if(p[i]==b) return (void*)(p+i);
    return NULL;
}

/* ══════════════════════════════════════════════════════════════
 * STRINGS
 * ══════════════════════════════════════════════════════════════ */

size_t strlen(const char *s) {
    const char *p=s;
    while((uintptr_t)p&7){if(!*p)return(size_t)(p-s);p++;}
    const uint64_t *wp=(const uint64_t*)p;
    for(;;){uint64_t w=*wp;if((w-0x0101010101010101ULL)&~w&0x8080808080808080ULL){const char*cp=(const char*)wp;while(*cp)cp++;return(size_t)(cp-s);}wp++;}
}

size_t strnlen(const char *s, size_t max) {
    for(size_t i=0;i<max;i++) if(!s[i]) return i;
    return max;
}

char *strcpy(char *d, const char *s) { char *r=d; while((*d++=*s++)); return r; }

char *strncpy(char *d, const char *s, size_t n) {
    size_t i=0;
    while(i<n&&s[i]){d[i]=s[i];i++;}
    while(i<n)d[i++]='\0';
    return d;
}

char *strcat(char *d, const char *s) { char *r=d+strlen(d); while((*r++=*s++)); return d; }

char *strncat(char *d, const char *s, size_t n) {
    char *r=d+strlen(d);
    size_t i=0;
    while(i<n&&s[i])r[i]=s[i],i++;
    r[i]='\0';
    return d;
}

int strcmp(const char *a, const char *b) {
    while(*a&&*a==*b){a++;b++;}
    return (unsigned char)*a-(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for(size_t i=0;i<n;i++){int d=(unsigned char)a[i]-(unsigned char)b[i];if(d||!a[i])return d;}
    return 0;
}

int strcasecmp(const char *a, const char *b) {
    while(*a&&tolower(*a)==tolower(*b)){a++;b++;}
    return tolower((unsigned char)*a)-tolower((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, size_t n) {
    for(size_t i=0;i<n;i++){int d=tolower((unsigned char)a[i])-tolower((unsigned char)b[i]);if(d||!a[i])return d;}
    return 0;
}

char *strchr(const char *s, int c) {
    char ch=(char)c;
    while(*s){if(*s==ch)return(char*)s;s++;}
    return ch=='\0'?(char*)s:NULL;
}

char *strrchr(const char *s, int c) {
    char ch=(char)c; const char *last=NULL;
    while(*s){if(*s==ch)last=s;s++;}
    return ch=='\0'?(char*)s:(char*)last;
}

char *strstr(const char *h, const char *n) {
    if(!*n) return(char*)h;
    size_t nlen=strlen(n),hlen=strlen(h);
    if(nlen>hlen) return NULL;
    for(size_t i=0;i<=hlen-nlen;i++)
        if(h[i]==n[0]&&memcmp(h+i,n,nlen)==0) return(char*)(h+i);
    return NULL;
}

char *strdup(const char *s) {
    size_t n=strlen(s)+1;
    char *p=(char*)malloc(n);
    if(p) memcpy(p,s,n);
    return p;
}

char *strndup(const char *s, size_t n) {
    size_t len=strnlen(s,n);
    char *p=(char*)malloc(len+1);
    if(p){memcpy(p,s,len);p[len]='\0';}
    return p;
}

size_t strlcpy(char *d, const char *s, size_t sz) {
    size_t slen=strlen(s);
    if(sz){size_t c=slen<sz-1?slen:sz-1;memcpy(d,s,c);d[c]='\0';}
    return slen;
}

size_t strlcat(char *d, const char *s, size_t sz) {
    size_t dlen=strnlen(d,sz);
    if(dlen==sz) return sz+strlen(s);
    size_t slen=strlen(s),avail=sz-dlen-1;
    size_t copy=slen<avail?slen:avail;
    memcpy(d+dlen,s,copy); d[dlen+copy]='\0';
    return dlen+slen;
}

char *strtok_r(char *s, const char *delim, char **save) {
    if(!s) s=*save;
    while(*s&&strchr(delim,*s)) s++;
    if(!*s){*save=s;return NULL;}
    char *tok=s;
    while(*s&&!strchr(delim,*s)) s++;
    if(*s){*s++='\0';}
    *save=s;
    return tok;
}

/* ══════════════════════════════════════════════════════════════
 * NUMBER CONVERSION
 * ══════════════════════════════════════════════════════════════ */

static int cdigit(char c, int base) {
    int d;
    if(isdigit(c))      d=c-'0';
    else if(isalpha(c)) d=tolower(c)-'a'+10;
    else return -1;
    return d<base?d:-1;
}

long strtol(const char *s, char **end, int base) {
    while(isspace((unsigned char)*s))s++;
    int neg=0;
    if(*s=='-'){neg=1;s++;}else if(*s=='+')s++;
    if(base==0){
        if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;}
        else if(s[0]=='0'&&s[1]){base=8;s++;}
        else base=10;
    } else if(base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;
    long v=0; int d;
    while((d=cdigit(*s,base))>=0){v=v*base+d;s++;}
    if(end)*end=(char*)s;
    return neg?-v:v;
}

unsigned long strtoul(const char *s, char **end, int base) {
    while(isspace((unsigned char)*s))s++;
    if(*s=='+')s++;
    if(base==0){
        if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;}
        else if(s[0]=='0'&&s[1]){base=8;s++;}
        else base=10;
    } else if(base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;
    unsigned long v=0; int d;
    while((d=cdigit(*s,base))>=0){v=v*(unsigned long)base+(unsigned long)d;s++;}
    if(end)*end=(char*)s;
    return v;
}

long long strtoll(const char *s, char **end, int base) { return (long long)strtol(s,end,base); }
int       atoi(const char *s)  { return (int)strtol(s,NULL,10); }
long      atol(const char *s)  { return strtol(s,NULL,10); }
long long atoll(const char *s) { return strtoll(s,NULL,10); }

double strtod(const char *s, char **end) {
    while(isspace((unsigned char)*s))s++;
    int neg=0;
    if(*s=='-'){neg=1;s++;}else if(*s=='+')s++;
    double v=0,frac=0,fdiv=1;
    while(isdigit(*s)){v=v*10+(*s-'0');s++;}
    if(*s=='.'){s++;while(isdigit(*s)){frac=frac*10+(*s-'0');fdiv*=10;s++;}}
    v+=frac/fdiv;
    if(*s=='e'||*s=='E'){
        s++;int eneg=0;
        if(*s=='-'){eneg=1;s++;}else if(*s=='+')s++;
        int exp=0;while(isdigit(*s)){exp=exp*10+(*s-'0');s++;}
        double ep=1;for(int i=0;i<exp;i++)ep*=10;
        if(eneg)v/=ep;else v*=ep;
    }
    if(end)*end=(char*)s;
    return neg?-v:v;
}
double atof(const char *s){return strtod(s,NULL);}

/* ══════════════════════════════════════════════════════════════
 * MATH  (pure C, no -lm)
 * ══════════════════════════════════════════════════════════════ */

double fabs(double x)  { return x<0?-x:x; }
double trunc(double x) { return (double)(int64_t)x; }
double floor(double x) { int64_t i=(int64_t)x; return(double)(i-(x<(double)i?1:0)); }
double ceil(double x)  { int64_t i=(int64_t)x; return(double)(i+(x>(double)i?1:0)); }
double round(double x) { return x>=0?floor(x+0.5):ceil(x-0.5); }
double fmod(double x, double y) { if(!y)return NAN; return x-trunc(x/y)*y; }
double fmin(double a, double b) { return a<b?a:b; }
double fmax(double a, double b) { return a>b?a:b; }

double sqrt(double x) {
    if(x<0)return NAN; if(x==0)return 0;
    uint64_t bits; __builtin_memcpy(&bits,&x,8);
    bits=(bits>>1)+0x1FF8000000000000ULL;
    double r; __builtin_memcpy(&r,&bits,8);
    r=0.5*(r+x/r); r=0.5*(r+x/r); r=0.5*(r+x/r); r=0.5*(r+x/r);
    return r;
}

double exp(double x) {
    /* Range reduce: e^x = e^(k*ln2) * e^r, |r|<=ln2/2 */
    const double LN2=0.6931471805599453;
    int k=(int)round(x/LN2);
    double r=x-(double)k*LN2;
    /* Horner for e^r */
    double er=1+r*(1+r*(0.5+r*(1.0/6+r*(1.0/24+r*(1.0/120+r/720)))));
    /* Multiply by 2^k */
    uint64_t bits; __builtin_memcpy(&bits,&er,8);
    bits=(uint64_t)((int64_t)bits+((int64_t)k<<52));
    __builtin_memcpy(&er,&bits,8);
    return er;
}

double log(double x) {
    if(x<=0) return x==0?-INFINITY:NAN;
    /* log(x) = log(m * 2^e) = log(m) + e*log(2), m in [1,2) */
    uint64_t bits; __builtin_memcpy(&bits,&x,8);
    int e=(int)((bits>>52)&0x7FF)-1023;
    bits=(bits&0x000FFFFFFFFFFFFFULL)|0x3FF0000000000000ULL;
    double m; __builtin_memcpy(&m,&bits,8);
    /* log(m) via Padé approx for m in [1,2) → reduce to [-0.5,0.5] */
    double y=(m-1)/(m+1);
    double y2=y*y;
    double lm=y*(2+y2*(2.0/3+y2*(2.0/5+y2*(2.0/7+y2*(2.0/9)))));
    return lm+(double)e*0.6931471805599453;
}

double log2(double x)  { return log(x)*1.4426950408889634; }
double log10(double x) { return log(x)*0.4342944819032518; }

double pow(double b, double e) {
    if(e==0)return 1; if(b==0)return 0;
    if(b<0){int64_t ie=(int64_t)e;if((double)ie!=e)return NAN;double r=pow(-b,e);return(ie&1)?-r:r;}
    return exp(e*log(b));
}

double sin(double x) {
    /* Range reduction to [-pi/2, pi/2] */
    x=fmod(x,2*M_PI);
    if(x>M_PI) x-=2*M_PI;
    if(x<-M_PI)x+=2*M_PI;
    if(x>M_PI/2) x=M_PI-x;
    if(x<-M_PI/2)x=-M_PI-x;
    double x2=x*x;
    return x*(1+x2*(-1.0/6+x2*(1.0/120+x2*(-1.0/5040+x2/362880))));
}

double cos(double x) { return sin(x+M_PI/2); }
double tan(double x) { double c=cos(x); return c?sin(x)/c:INFINITY; }

double atan2(double y, double x) {
    if(x>0)  { double r=y/x,r2=r*r; return r*(1+r2*(-1.0/3+r2*(1.0/5+r2*(-1.0/7+r2/9)))); }
    if(x<0&&y>=0) return atan2(y,-x)+M_PI;
    if(x<0&&y<0)  return atan2(y,-x)-M_PI;
    if(y>0) return  M_PI/2;
    if(y<0) return -M_PI/2;
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * SORT
 * ══════════════════════════════════════════════════════════════ */

#define ISORT_THRESH 16
#define ELEM(b,i,s) ((char*)(b)+(i)*(s))

static void swap_e(void *a, void *b, size_t sz) {
    if(sz==4){uint32_t t;__builtin_memcpy(&t,a,4);__builtin_memcpy(a,b,4);__builtin_memcpy(b,&t,4);return;}
    if(sz==8){uint64_t t;__builtin_memcpy(&t,a,8);__builtin_memcpy(a,b,8);__builtin_memcpy(b,&t,8);return;}
    unsigned char *pa=(unsigned char*)a,*pb=(unsigned char*)b;
    for(size_t i=0;i<sz;i++){unsigned char t=pa[i];pa[i]=pb[i];pb[i]=t;}
}

static void isort(void *base,size_t lo,size_t hi,size_t sz,__cmp_fn cmp){
    for(size_t i=lo+1;i<hi;i++){
        char *key=(char*)malloc(sz); if(!key)return;
        memcpy(key,ELEM(base,i,sz),sz);
        size_t j=i;
        while(j>lo&&cmp(ELEM(base,j-1,sz),key)>0){memcpy(ELEM(base,j,sz),ELEM(base,j-1,sz),sz);j--;}
        memcpy(ELEM(base,j,sz),key,sz); free(key);
    }
}

static void qsort_r(void *base,size_t lo,size_t hi,size_t sz,__cmp_fn cmp){
    while(hi-lo>ISORT_THRESH){
        size_t mid=lo+(hi-lo)/2;
        /* median-of-3 */
        if(cmp(ELEM(base,lo,sz),ELEM(base,mid,sz))>0) swap_e(ELEM(base,lo,sz),ELEM(base,mid,sz),sz);
        if(cmp(ELEM(base,lo,sz),ELEM(base,hi-1,sz))>0) swap_e(ELEM(base,lo,sz),ELEM(base,hi-1,sz),sz);
        if(cmp(ELEM(base,mid,sz),ELEM(base,hi-1,sz))>0) swap_e(ELEM(base,mid,sz),ELEM(base,hi-1,sz),sz);
        swap_e(ELEM(base,mid,sz),ELEM(base,lo,sz),sz);
        /* 3-way partition */
        size_t lt=lo,gt=hi,i=lo+1;
        while(i<gt){
            int c=cmp(ELEM(base,i,sz),ELEM(base,lo,sz));
            if(c<0){swap_e(ELEM(base,lt,sz),ELEM(base,i,sz),sz);lt++;i++;}
            else if(c>0){gt--;swap_e(ELEM(base,i,sz),ELEM(base,gt,sz),sz);}
            else i++;
        }
        swap_e(ELEM(base,lo,sz),ELEM(base,lt>lo?lt-1:lo,sz),sz);
        if(lt>lo)lt--;
        size_t ls=lt>lo?lt-lo:0,rs=hi>gt?hi-gt:0;
        if(ls<rs){if(ls>1)qsort_r(base,lo,lt,sz,cmp);lo=gt;}
        else{if(rs>1)qsort_r(base,gt,hi,sz,cmp);hi=lt;}
    }
    if(hi>lo+1)isort(base,lo,hi,sz,cmp);
}

void qsort(void *base,size_t n,size_t sz,__cmp_fn cmp){if(n>1)qsort_r(base,0,n,sz,cmp);}

void *bsearch(const void *key,const void *base,size_t n,size_t sz,__cmp_fn cmp){
    size_t lo=0,hi=n;
    while(lo<hi){size_t mid=lo+(hi-lo)/2;int c=cmp(key,ELEM(base,mid,sz));if(c<0)hi=mid;else if(c>0)lo=mid+1;else return(void*)ELEM(base,mid,sz);}
    return NULL;
}
