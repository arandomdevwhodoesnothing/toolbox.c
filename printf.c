/*
 * libc/src/printf.c
 * printf family — vsnprintf, printf, fprintf, sprintf, snprintf
 * No external dependencies.
 */

#include "../include/libtool.h"

/* ── Output buffer ───────────────────────────────────────────────────── */
typedef struct {
    char   *buf;
    size_t  cap;
    size_t  pos;
    FILE   *fp;    /* if non-NULL, flush to file instead */
} fmtbuf_t;

static void fb_putc(fmtbuf_t *fb, char c) {
    if (fb->fp) {
        fputc(c, fb->fp);
        fb->pos++;
        return;
    }
    if (fb->pos + 1 < fb->cap) fb->buf[fb->pos] = c;
    fb->pos++;
}

static void fb_puts(fmtbuf_t *fb, const char *s, size_t n) {
    if (fb->fp) {
        fwrite(s, 1, n, fb->fp);
        fb->pos += n;
        return;
    }
    for (size_t i = 0; i < n; i++) fb_putc(fb, s[i]);
}

static void fb_pad(fmtbuf_t *fb, char c, int n) {
    for (int i = 0; i < n; i++) fb_putc(fb, c);
}

/* ── Flags ───────────────────────────────────────────────────────────── */
#define FL_MINUS  1
#define FL_PLUS   2
#define FL_SPACE  4
#define FL_ZERO   8
#define FL_HASH   16
#define FL_UPPER  32

/* ── Integer formatter ───────────────────────────────────────────────── */
static void fmt_uint(fmtbuf_t *fb, uint64_t v, int base, int flags,
                     int width, const char *pfx) {
    static const char lo[] = "0123456789abcdef";
    static const char hi[] = "0123456789ABCDEF";
    const char *digs = (flags & FL_UPPER) ? hi : lo;
    char tmp[66]; int len = 0;
    if (v == 0) tmp[len++] = '0';
    while (v > 0) { tmp[len++] = digs[v % (uint64_t)base]; v /= (uint64_t)base; }
    int pfxlen = pfx ? (int)strlen(pfx) : 0;
    int total  = pfxlen + len;
    int pad    = width > total ? width - total : 0;
    char pc    = (flags & FL_ZERO) && !(flags & FL_MINUS) ? '0' : ' ';
    if (!(flags & FL_MINUS)) {
        if (pc == ' ') fb_pad(fb, ' ', pad);
        if (pfx) fb_puts(fb, pfx, (size_t)pfxlen);
        if (pc == '0') fb_pad(fb, '0', pad);
    } else {
        if (pfx) fb_puts(fb, pfx, (size_t)pfxlen);
    }
    for (int i = len-1; i >= 0; i--) fb_putc(fb, tmp[i]);
    if (flags & FL_MINUS) fb_pad(fb, ' ', pad);
}

static void fmt_int(fmtbuf_t *fb, int64_t v, int flags, int width) {
    char pfx[4] = {0}; int pi = 0;
    uint64_t uv;
    if (v < 0) { pfx[pi++]='-'; uv=(uint64_t)(-(v+1))+1; }
    else { if (flags&FL_PLUS) pfx[pi++]='+'; else if(flags&FL_SPACE) pfx[pi++]=' '; uv=(uint64_t)v; }
    pfx[pi]='\0';
    fmt_uint(fb, uv, 10, flags & ~(FL_PLUS|FL_SPACE), width, pi?pfx:NULL);
}

/* ── Float formatter ─────────────────────────────────────────────────── */
static void fmt_float(fmtbuf_t *fb, double v, int flags, int width, int prec) {
    if (prec < 0) prec = 6;
    char tmp[64];
    int  len = 0;
    /* Handle sign */
    if (v < 0.0) { tmp[len++]='-'; v=-v; }
    else if (flags&FL_PLUS) tmp[len++]='+';
    else if (flags&FL_SPACE) tmp[len++]=' ';
    /* Integer part */
    uint64_t ipart = (uint64_t)v;
    double   fpart = v - (double)ipart;
    char ibuf[32]; int ilen=0;
    if (ipart==0) ibuf[ilen++]='0';
    while (ipart>0) { ibuf[ilen++]='0'+(int)(ipart%10); ipart/=10; }
    for (int i=ilen-1;i>=0;i--) tmp[len++]=ibuf[i];
    if (prec > 0) {
        tmp[len++]='.';
        for (int i=0;i<prec;i++) { fpart*=10.0; int d=(int)fpart; tmp[len++]='0'+d; fpart-=d; }
    }
    int pad = width > len ? width - len : 0;
    char pc = (flags&FL_ZERO)&&!(flags&FL_MINUS) ? '0' : ' ';
    if (!(flags&FL_MINUS)) fb_pad(fb, pc, pad);
    fb_puts(fb, tmp, (size_t)len);
    if (flags&FL_MINUS) fb_pad(fb, ' ', pad);
}

/* ── Length modifiers ────────────────────────────────────────────────── */
typedef enum { LEN_NONE,LEN_HH,LEN_H,LEN_L,LEN_LL,LEN_Z,LEN_T } lenmod_t;

/* ── Core formatter ──────────────────────────────────────────────────── */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    fmtbuf_t fb = { buf, size, 0, NULL };

    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { fb_putc(&fb, *f); continue; }
        f++;

        int flags=0;
        for(;;){
            if      (*f=='-'){flags|=FL_MINUS;f++;}
            else if (*f=='+'){flags|=FL_PLUS; f++;}
            else if (*f==' '){flags|=FL_SPACE;f++;}
            else if (*f=='0'){flags|=FL_ZERO; f++;}
            else if (*f=='#'){flags|=FL_HASH; f++;}
            else break;
        }
        int width=0;
        if (*f=='*'){width=va_arg(ap,int);if(width<0){flags|=FL_MINUS;width=-width;}f++;}
        else while(isdigit(*f)){width=width*10+(*f-'0');f++;}
        int prec=-1;
        if(*f=='.'){f++;prec=0;if(*f=='*'){prec=va_arg(ap,int);f++;}else while(isdigit(*f)){prec=prec*10+(*f-'0');f++;}}

        lenmod_t len=LEN_NONE;
        if     (*f=='h'){f++;if(*f=='h'){len=LEN_HH;f++;}else len=LEN_H;}
        else if(*f=='l'){f++;if(*f=='l'){len=LEN_LL;f++;}else len=LEN_L;}
        else if(*f=='z'){len=LEN_Z;f++;}
        else if(*f=='t'){len=LEN_T;f++;}

        switch(*f) {
        case 'd': case 'i': {
            int64_t v;
            if     (len==LEN_LL) v=va_arg(ap,long long);
            else if(len==LEN_L)  v=va_arg(ap,long);
            else if(len==LEN_HH) v=(int8_t)va_arg(ap,int);
            else if(len==LEN_H)  v=(int16_t)va_arg(ap,int);
            else if(len==LEN_Z)  v=(int64_t)va_arg(ap,size_t);
            else                 v=va_arg(ap,int);
            fmt_int(&fb, v, flags, width); break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            uint64_t v;
            if     (len==LEN_LL) v=va_arg(ap,unsigned long long);
            else if(len==LEN_L)  v=va_arg(ap,unsigned long);
            else if(len==LEN_HH) v=(uint8_t)va_arg(ap,unsigned);
            else if(len==LEN_H)  v=(uint16_t)va_arg(ap,unsigned);
            else if(len==LEN_Z)  v=va_arg(ap,size_t);
            else                 v=va_arg(ap,unsigned);
            int base=(*f=='o')?8:(*f=='x'||*f=='X')?16:10;
            if(*f=='X') flags|=FL_UPPER;
            char pfxbuf[4]={0};
            const char *pfx=NULL;
            if((flags&FL_HASH)&&v){
                if(*f=='o'){pfxbuf[0]='0';pfx=pfxbuf;}
                else if(*f=='x'){pfxbuf[0]='0';pfxbuf[1]='x';pfx=pfxbuf;}
                else if(*f=='X'){pfxbuf[0]='0';pfxbuf[1]='X';pfx=pfxbuf;}
            }
            fmt_uint(&fb, v, base, flags, width, pfx); break;
        }
        case 'f': case 'F': {
            double v=va_arg(ap,double);
            fmt_float(&fb, v, flags, width, prec); break;
        }
        case 'e': case 'E': case 'g': case 'G': {
            double v=va_arg(ap,double);
            fmt_float(&fb, v, flags, width, prec); break;
        }
        case 'c': {
            char c=(char)va_arg(ap,int);
            int pad=width>1?width-1:0;
            if(!(flags&FL_MINUS)) fb_pad(&fb,' ',pad);
            fb_putc(&fb,c);
            if(flags&FL_MINUS)  fb_pad(&fb,' ',pad);
            break;
        }
        case 's': {
            const char *s=va_arg(ap,const char*);
            if(!s) s="(null)";
            size_t slen=strlen(s);
            if(prec>=0&&(size_t)prec<slen) slen=(size_t)prec;
            int pad=width>(int)slen?width-(int)slen:0;
            if(!(flags&FL_MINUS)) fb_pad(&fb,' ',pad);
            fb_puts(&fb,s,slen);
            if(flags&FL_MINUS)   fb_pad(&fb,' ',pad);
            break;
        }
        case 'p': {
            uint64_t v=(uint64_t)(uintptr_t)va_arg(ap,void*);
            fmt_uint(&fb, v, 16, flags, width, "0x"); break;
        }
        case 'n': { int *n=va_arg(ap,int*); if(n)*n=(int)fb.pos; break; }
        case '%': fb_putc(&fb,'%'); break;
        default:  fb_putc(&fb,'%'); fb_putc(&fb,*f); break;
        }
    }
    if (size > 0) { size_t t=fb.pos<size?fb.pos:size-1; buf[t]='\0'; }
    return (int)fb.pos;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    int r=vsnprintf(buf,size,fmt,ap);
    va_end(ap); return r;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    int r=vsnprintf(buf,(size_t)-1,fmt,ap);
    va_end(ap); return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
    /* Two-pass: measure then write */
    va_list ap2; va_copy(ap2,ap);
    int len=vsnprintf(NULL,0,fmt,ap);
    if(len<=0){va_end(ap2);return len;}
    char *buf=(char*)malloc((size_t)(len+1));
    if(!buf){va_end(ap2);return -1;}
    vsnprintf(buf,(size_t)(len+1),fmt,ap2);
    va_end(ap2);
    fwrite(buf,1,(size_t)len,f);
    free(buf);
    return len;
}

int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout,fmt,ap); }

int fprintf(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    int r=vfprintf(f,fmt,ap);
    va_end(ap); return r;
}

int printf(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    int r=vfprintf(stdout,fmt,ap);
    va_end(ap); return r;
}

char *asprintf_alloc(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    int len=vsnprintf(NULL,0,fmt,ap);
    va_end(ap);
    if(len<0) return NULL;
    char *buf=(char*)malloc((size_t)(len+1));
    if(!buf) return NULL;
    va_start(ap,fmt);
    vsnprintf(buf,(size_t)(len+1),fmt,ap);
    va_end(ap);
    return buf;
}
