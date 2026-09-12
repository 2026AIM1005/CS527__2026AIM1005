#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#ifdef _WIN32
#define strncasecmp _strnicmp
#else
#include <strings.h>
#endif
#include "compiler.h"

#define MAX_LINES 4096
#define MAX_LABELS 512
#define MAX_TOKENS 8

typedef struct { char name[64]; int address; } Label;
typedef struct { unsigned char b[4]; } Code;
static Label labels[MAX_LABELS];
static int label_count;
static Code code[MAX_LINES];
static int code_count;

static void trim(char *s)
{
    char *p = strchr(s, '%');
    int n;
    if (p) *p = '\0';
    n = (int)strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n]='\0';
    p=s; while (*p && isspace((unsigned char)*p)) ++p;
    if (p!=s) memmove(s,p,strlen(p)+1);
}

static void clean_token(char *s)
{
    int n=(int)strlen(s);
    while(n && (s[n-1]==';'||s[n-1]==','||isspace((unsigned char)s[n-1]))) s[--n]='\0';
    if (s[0]=='[') {
        n=(int)strlen(s);
        if(n>1 && s[n-1]==']'){ memmove(s,s+1,n-2); s[n-2]='\0'; }
    }
}

static int reg_number(const char *s)
{
    char *end; long v;
    if(s[0]!='x' || !isdigit((unsigned char)s[1])) return -1;
    v=strtol(s+1,&end,10);
    return (*end=='\0' && v>=0 && v<256) ? (int)v : -1;
}
static int vec_number(const char *s)
{
    char *end; long v;
    if(s[0]!='v' || !isdigit((unsigned char)s[1])) return -1;
    v=strtol(s+1,&end,10);
    return (*end=='\0' && v>=0 && v<32) ? (int)v : -1;
}
static int constant_number(const char *s)
{
    char *end; long v=strtol(s,&end,0);
    if(*s=='\0' || *end!='\0' || v<0 || v>255) return -1;
    return (int)v;
}
static int tokenize(char *line,char tok[][64])
{
    char *p=strtok(line," \t=+*-/%[](),;"); int n=0;
    while(p && n<MAX_TOKENS){ strncpy(tok[n],p,63); tok[n][63]='\0'; ++n; p=strtok(NULL," \t=+*-/%[](),;"); }
    return n;
}
static char operation(const char *line)
{
    if (strchr(line, '+')) return '+';
    if (strchr(line, '-')) return '-';
    if (strchr(line, '*')) return '*';
    if (strchr(line, '/')) return '/';
    return 0;
}
static int find_label(const char *name)
{
    int i; for(i=0;i<label_count;++i) if(!strcmp(labels[i].name,name)) return labels[i].address;
    return -1;
}
static void emit4(unsigned char a,unsigned char b,unsigned char c,unsigned char d)
{
    if(code_count>=MAX_LINES){ fprintf(stderr,"Program too large.\n"); return; }
    code[code_count].b[0]=a; code[code_count].b[1]=b; code[code_count].b[2]=c; code[code_count].b[3]=d; ++code_count;
}

static int compile_line(char *line,int pass)
{
    char original[512], tok[MAX_TOKENS][64], name[64];
    int n,d,v1,r1,r2,c1,c2;
    char op;
    strcpy(original,line); trim(line); if(!line[0]) return 1;
    if(line[0]=='.'){
        if(pass==1){ if(label_count>=MAX_LABELS) return 0; strncpy(labels[label_count].name,line+1,63); labels[label_count].name[63]='\0'; labels[label_count].address=code_count*4; ++label_count; }
        return 1;
    }

    if(!strncasecmp(line,"Print ",6)){
        n=tokenize(line+6,tok); if(n!=1) return 0; r1=reg_number(tok[0]); if(r1<0) return 0;
        if(pass==2) emit4(0x08,0,0,(unsigned char)r1); else ++code_count; return 1;
    }

    if(!strncasecmp(line,"BAL ",4)||!strncasecmp(line,"BEQ ",4)||!strncasecmp(line,"BNE ",4)||!strncasecmp(line,"BGE ",4)||!strncasecmp(line,"BLT ",4)||!strncasecmp(line,"BGT ",4)||!strncasecmp(line,"BLE ",4)){
        int cond=0, target;
        char *sp=strchr(line,' '); if(!sp) return 0; strncpy(name,sp+1,63); name[63]='\0'; clean_token(name);
        if(!strncasecmp(line,"BEQ",3)) cond=1; else if(!strncasecmp(line,"BNE",3)) cond=2; else if(!strncasecmp(line,"BGE",3)) cond=3; else if(!strncasecmp(line,"BLT",3)) cond=4; else if(!strncasecmp(line,"BGT",3)) cond=5; else if(!strncasecmp(line,"BLE",3)) cond=6;
        if(pass==1){ ++code_count; return 1; }
        target=find_label(name[0]=='.'?name+1:name); if(target<0){fprintf(stderr,"Unknown label: %s\n",name);return 0;}
        { int current=code_count*4, offset=(target-(current+4))/4; emit4((unsigned char)(0x10+cond),0,0,(unsigned char)(int8_t)offset); }
        return 1;
    }

    if(!strncasecmp(line,"Read ",5)||!strncasecmp(line,"Write ",6)){
        char *p=line+(!strncasecmp(line,"Read ",5)?5:6); n=tokenize(p,tok); if(n<2) return 0;
        d=reg_number(tok[0]); c1=constant_number(tok[1]); if(d<0||c1<0) return 0;
        /*
         * Legacy Lab-1 syntax is kept compatible with the later bracket
         * syntax.  src1=0 marks an immediate legacy address and src2 holds
         * that address.  Bracket form uses src1 as an address register and
         * keeps src2=0.
         */
        if (pass == 2)
        {
            if (!strncasecmp(line, "Read ", 5))
                emit4(0x05, (unsigned char)d, 0, (unsigned char)c1);
            else
                emit4(0x06, (unsigned char)d, 0, (unsigned char)c1);
        }
        else
            ++code_count;
        return 1;
    }

    n=tokenize(line,tok); if(n<2){fprintf(stderr,"Cannot parse: %s\n",original);return 0;}
    clean_token(tok[0]); d=reg_number(tok[0]); v1=vec_number(tok[0]);

    if(strchr(original,'[')){
        int addrreg=reg_number(tok[1]), srcreg=reg_number(tok[1]), srcvec=vec_number(tok[1]), addrconst=constant_number(tok[1]);
        int is_store=(original[0]=='[');
        if(!is_store){
            if(v1>=0){
                if(addrreg>=0){ if(pass==2) emit4(0x25,(unsigned char)v1,(unsigned char)addrreg,0); else ++code_count; return 1; }
                if(addrconst>=0){ if(pass==2){ emit4(0x07,255,0,(unsigned char)addrconst); emit4(0x25,(unsigned char)v1,255,0);} else code_count+=2; return 1; }
            } else if(d>=0){
                if(addrreg>=0){ if(pass==2) emit4(0x05,(unsigned char)d,(unsigned char)addrreg,0); else ++code_count; return 1; }
                if(addrconst>=0){ if(pass==2){ emit4(0x07,255,0,(unsigned char)addrconst); emit4(0x05,(unsigned char)d,255,0);} else code_count+=2; return 1; }
            }
        } else {
            if(d>=0 && srcreg>=0){ if(pass==2) emit4(0x06,(unsigned char)d,(unsigned char)srcreg,0); else ++code_count; return 1; }
            if(d>=0 && srcvec>=0){ if(pass==2) emit4(0x26,(unsigned char)d,0,(unsigned char)srcvec); else ++code_count; return 1; }
        }
        return 0;
    }

    op=operation(original);
    if(!op && v1<0){ c1=constant_number(tok[1]); if(d<0||c1<0) return 0; if(pass==2) emit4(0x07,(unsigned char)d,0,(unsigned char)c1); else ++code_count; return 1; }

    if(v1>=0){
        r1=vec_number(tok[1]); r2=vec_number(tok[2]); c2=constant_number(tok[2]); int scalar=reg_number(tok[2]); unsigned char opc;
        if(r1<0) return 0;
        if(r2>=0){ if(op=='+')opc=0x21;else if(op=='-')opc=0x22;else if(op=='*')opc=0x23;else return 0; if(pass==2)emit4(opc,(unsigned char)v1,(unsigned char)r1,(unsigned char)r2);else ++code_count; }
        else if(scalar>=0){ if(op!='*')return 0; if(pass==2)emit4(0x2B,(unsigned char)v1,(unsigned char)r1,(unsigned char)scalar);else ++code_count; }
        else { if(c2<0)return 0; if(op=='+')opc=0x29;else if(op=='-')opc=0x2A;else if(op=='*')opc=0x2B;else return 0; if(pass==2)emit4(opc,(unsigned char)v1,(unsigned char)r1,(unsigned char)c2);else ++code_count; }
        return 1;
    }

    if(op){
        int left_const=0,right_const=0;
        c1=constant_number(tok[1]); r1=reg_number(tok[1]); c2=constant_number(tok[2]); r2=reg_number(tok[2]);
        left_const=(r1<0); right_const=(r2<0);
        if(d<0||left_const||(!right_const&&r2<0)|| (right_const&&c2<0)) return 0;
        { unsigned char opc; if(op=='+')opc=right_const?0x09:0x01; else if(op=='-')opc=right_const?0x0A:0x02; else if(op=='*')opc=right_const?0x0B:0x03; else if(op=='/')opc=right_const?0x0C:0x04; else return 0; if(pass==2)emit4(opc,(unsigned char)d,(unsigned char)r1,(unsigned char)(right_const?c2:r2));else ++code_count; }
        (void)c1;
        return 1;
    }
    fprintf(stderr,"Unsupported line: %s\n",original); return 0;
}

int compile_file(const char *source_file,const char *byte_file)
{
    FILE *fp,*out; char line[512]; int pass,i;
    label_count=0;
    for(pass=1;pass<=2;++pass){
        fp=fopen(source_file,"r"); if(!fp){perror(source_file);return 0;} code_count=0;
        while(fgets(line,sizeof(line),fp)) if(!compile_line(line,pass)){fclose(fp);return 0;}
        fclose(fp);
        if(pass==1){ if(code_count>=MAX_LINES-1){fprintf(stderr,"Program too large.\n");return 0;} code[code_count].b[0]=code[code_count].b[1]=code[code_count].b[2]=code[code_count].b[3]=0; ++code_count; }
    }
    /* Append the HALT instruction in the emitted second-pass output too. */
    if (code_count >= MAX_LINES) { fprintf(stderr,"Program too large.\n"); return 0; }
    code[code_count].b[0] = 0x00;
    code[code_count].b[1] = code[code_count].b[2] = code[code_count].b[3] = 0;
    ++code_count;

    out=fopen(byte_file,"w"); if(!out){perror(byte_file);return 0;}
    for(i=0;i<code_count;++i) fprintf(out,"%02X %02X %02X %02X\n",code[i].b[0],code[i].b[1],code[i].b[2],code[i].b[3]);
    fclose(out);
    printf("Compiled %s -> %s (%d instructions)\n",source_file,byte_file,code_count);
    return 1;
}
