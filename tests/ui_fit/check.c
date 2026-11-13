/* The shared fit policy (include/breadbox/ui_fit.h), production code:
 * never below the floor, never wider than the room, never a broken UTF-8 character, "..." exactly when cut. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "ui_fit.h"
/* a fake font: ASCII 10 px per unit of size, every other code point (CJK, emoji, accents) 20 px */
static float measure(const char *s, float size, void *ud)
{
	(void)ud;float w=0;
	for(const unsigned char *p=(const unsigned char *)s;*p;p++){if((*p&0xc0)==0x80)continue;w+=(*p<0x80?10:20)*size;}
	return w;
}
static bool valid_utf8(const char *s)
{
	for(const unsigned char *p=(const unsigned char *)s;*p;){
		int n=*p<0x80?1:(*p>>5)==6?2:(*p>>4)==14?3:(*p>>3)==30?4:0;if(!n)return false;
		for(int i=1;i<n;i++){if((p[i]&0xc0)!=0x80)return false;}
		p+=n;
	}
	return true;
}
static void check(const char *s,float size,float min,float width,size_t cap)
{
	char out[600];bool cut;assert(cap<=sizeof(out));
	float got=ui_fit_bounded(s,size,min,width,measure,NULL,out,cap,&cut);
	assert(got>=min-1e-6f && got<=size+1e-6f);                   /* never below the floor */
	assert(strlen(out)<cap && valid_utf8(out));                   /* terminated, whole characters */
	assert(measure(out,got,NULL)<=width+1e-3f);                   /* never wider than the room */
	size_t n=strlen(out),e=strlen(UI_FIT_ELLIPSIS);
	if(cut){assert(got==min);if(n){assert(n>=e && !strcmp(out+n-e,UI_FIT_ELLIPSIS));assert(!strncmp(out,s,n-e));}}
	else assert(!strcmp(out,s));
}

/* ui_fit_lines_bounded: the Continue postcard's name rule (one line, wrap, floor, cut) and its balance */
static void lines_check(const char *s,float size,float min,float width,int max)
{
	ui_fit_line out[4];float got;
	int n=ui_fit_lines_bounded(s,size,min,width,max,measure,NULL,out,&got);
	assert(n>=0 && n<=max);
	char joined[600]="";bool cut=false;
	for(int i=0;i<n;i++){
		assert(valid_utf8(out[i]) && out[i][0] && out[i][0]!=' ');
		assert(measure(out[i],got,NULL)<=width+1e-3f);                     /* every line inside the width */
		size_t k=strlen(out[i]);if(k>=3 && !strcmp(out[i]+k-3,UI_FIT_ELLIPSIS))cut=true;
		if(i){strcat(joined," ");}
		strcat(joined,out[i]);
	}
	const char *t=s;while(*t==' ')t++;
	if(!*t){assert(n==0);return;}
	char a[600],b[600];size_t na=0,nb=0;                                   /* nothing lost unless cut (breaks drop spaces) */
	for(const char *q=joined;*q;q++)if(*q!=' ')a[na++]=*q;
	for(const char *q=t;*q;q++)if(*q!=' ')b[nb++]=*q;
	a[na]=b[nb]=0;
	if(!cut){assert(!strcmp(a,b));}
	else{assert(n==max && got==min);}                                       /* cut only at the floor, on the last line */
	if(n==1 && !cut && got==size)return;
	if(!cut){
		/* fewest lines: at size when they fit there, else at the floor */
		int need=ui_fit_wrap(t,got,width,0,measure,NULL,NULL);assert(need==n);
		if(got<size)assert(ui_fit_wrap(t,size,width,0,measure,NULL,NULL)>max);
		/* balanced: one pixel narrower than its widest line needs another line (no narrower layout keeps n) */
		float widest=0;for(int i=0;i<n;i++)if(measure(out[i],got,NULL)>widest)widest=measure(out[i],got,NULL);
		if(n>1 && widest-1>=width/2)assert(ui_fit_wrap(t,got,widest-1,0,measure,NULL,NULL)>n);
	}
}
int main(void)
{
	/* fits as is; shrinks within the floor; cut at the floor */
	char out[64];bool cut;
	assert(ui_fit_bounded("Games folder",.7f,.7f,200,measure,NULL,out,sizeof(out),&cut)==.7f && !cut && !strcmp(out,"Games folder"));
	float s=ui_fit_bounded("Games folder",.8f,.7f,12*10*.75f,measure,NULL,out,sizeof(out),&cut);assert(s<.8f && s>=.7f && !cut);
	ui_fit_bounded("Games folder",.7f,.7f,50,measure,NULL,out,sizeof(out),&cut);assert(cut && !strcmp(out,"Game..."));   /* 7 px a letter at the floor */
	/* trailing spaces are not kept before "..." */
	ui_fit_bounded("Ab  cdefgh",.7f,.7f,(5)*7,measure,NULL,out,sizeof(out),&cut);assert(cut && !strcmp(out,"Ab..."));
	/* nothing fits, not even "...": empty, never wider */
	ui_fit_bounded("Anything",.7f,.7f,10,measure,NULL,out,sizeof(out),&cut);assert(cut && !out[0]);
	/* maximum-length ASCII (a 255-byte file name), no spaces, at every width */
	char ascii[256];memset(ascii,'W',255);ascii[255]=0;
	char longname[300];snprintf(longname,sizeof(longname),"%s",ascii);
	/* multibyte: accents, CJK without spaces, 4-byte emoji, a mix */
	const char *texts[]={ascii,"Pok\xc3\xa9mon - Mystery Dungeon - Red Rescue Team (USA, Australia)",
		"\xe5\x86\x92\xe9\x99\xba\xe3\x81\xa8\xe9\x9f\xb3\xe6\xa5\xbd\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0\xe3\x81\xae\xe6\x9c\xac\xe6\xa3\x9a\xe3\x81\x8a\xe6\xb0\x97\xe3\x81\xab\xe5\x85\xa5\xe3\x82\x8a",
		"\xf0\x9f\x8d\x9e\xf0\x9f\x8d\x9e\xf0\x9f\x8d\x9e" "BreadBox\xf0\x9f\x8d\x9e",
		"sdmc:/games/\xe3\x82\xb2\xe3\x83\xbc\xe3\x83\xa0/ThisIsAVeryLongBasenameWithoutAnySpacesAtAllThatGoesOnAndOn",
		"Choose automatic updates or check now.","",longname};
	const float widths[]={0,5,21,40,73,100,150,214,243,320,4000};
	const size_t caps[]={1,2,4,5,8,16,48,96,300,600};
	for(unsigned t=0;t<sizeof(texts)/sizeof(*texts);t++)
		for(unsigned w=0;w<sizeof(widths)/sizeof(*widths);w++)
			for(unsigned c=0;c<sizeof(caps)/sizeof(*caps);c++)
				check(texts[t],.8f,.7f,widths[w],caps[c]);
	/* a byte-limited copy never ends inside a character */
	char small[8];
	for(size_t m=0;m<20;m++){ui_fit_copy(small,sizeof(small),texts[2],m);assert(valid_utf8(small) && strlen(small)<=m);}
	puts("ui_fit PASS: floor kept, width kept, whole UTF-8 characters, '...' exactly when cut; 255-byte ASCII, accents, CJK without spaces, emoji, long basenames");
	/* the Continue postcard's names (devtools continue-card NOTES.md), a one-word and an empty name: at the
	 * postcard's .8 / floor .7 sizes over a width that wraps them, and the shipped 284 px */
	const char *names[]={"Crash Bandicoot","Kirby - Nightmare in Dream Land",
		"The Legend of Zelda - A Link to the Past & Four Swords","Tom Clancy's Splinter Cell - Pandora Tomorrow & Chaos Theory",
		"Tetris","","   ",longname,texts[2]};
	const float lw[]={30,60,140,150,196,284,400};
	for(unsigned t=0;t<sizeof(names)/sizeof(*names);t++)
		for(unsigned w=0;w<sizeof(lw)/sizeof(*lw);w++)
			for(int m=1;m<=3;m++)lines_check(names[t],.7f,.7f,lw[w],m),lines_check(names[t],.8f,.7f,lw[w],m);
	{	/* balanced, not greedy: "Kirby - Nightmare in" / "Dream Land" becomes "Kirby - Nightmare" / "in Dream Land" */
		ui_fit_line out[3];float got;
		int n=ui_fit_lines_bounded(names[1],.7f,.7f,150,3,measure,NULL,out,&got);
		assert(n==2 && got==.7f && !strcmp(out[0],"Kirby - Nightmare") && !strcmp(out[1],"in Dream Land"));
		assert(ui_fit_lines_bounded(names[0],.8f,.7f,284,3,measure,NULL,out,&got)==1 && got==.8f && !strcmp(out[0],names[0]));
		assert(ui_fit_lines_bounded("Tetris",.7f,.7f,284,3,measure,NULL,out,&got)==1 && !strcmp(out[0],"Tetris"));
		assert(ui_fit_lines_bounded("",.7f,.7f,284,3,measure,NULL,out,&got)==0);
		/* too long for three lines even at the floor: three lines, the last cut with "..." */
		n=ui_fit_lines_bounded(names[3],.7f,.7f,100,3,measure,NULL,out,&got);
		assert(n==3 && got==.7f && strstr(out[2],UI_FIT_ELLIPSIS));
		/* one word wider than the line: broken between characters */
		n=ui_fit_lines_bounded("Supercalifragilistic",.7f,.7f,70,3,measure,NULL,out,&got);
		assert(n==2 && !strcmp(out[0],"Supercalif") && !strcmp(out[1],"ragilistic"));
	}
	puts("ui_fit_lines PASS: four Continue names, one word, empty: fewest lines, size then floor, balanced, cut only at the floor on the last line");
	return 0;
}
