/* Copyright (C) 2003-2009 Datapark corp. All rights reserved.
   Copyright (C) 2000-2002 Lavtech.com corp. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/
#include "dpsearch.h"
#include "dps_conf.h"
#include "dps_db.h"
#include "dps_http.h"
#include "dps_parsehtml.h"
#include "dps_host.h"
#include "dps_contentencoding.h"
#include "dps_utils.h"
#include "dps_wild.h"
#include "dps_chinese.h"
#include "dps_acronym.h"
#include "dps_charsetutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>

/******************* Template functions ********************/


static size_t out_string(DPS_OUTPUTFUNCTION dps_out, void *stream, char * dst, size_t dst_len, const char * src) {
	if(src){
		if(stream) dps_out(stream, "%s", src);
		if(dst){
			dps_strncat(dst, src, dst_len - 1);
			return(dps_strlen(src));
		}
	}
	return 0;
}

static char * HiLightDup(const char * src,const char * beg, const char * end){
	size_t len=15;
	size_t blen=dps_strlen(beg);
	size_t elen=dps_strlen(end);
	const char * s;
	char  * res, *d;
	
	for(s=src;*s;s++){
		switch(*s){
			case '\2':
				len+=blen;
				break;
			case '\3':
				len+=elen;
				break;
			default:
				len++;
		}
	}
	res = (char*)DpsMalloc(len + 1);
	if (res == NULL) return NULL;
	for(s=src,d=res;*s;s++){
		switch(*s){
			case '\2':
				dps_strcpy(d,beg);
				d+=blen;
				break;
			case '\3':
				dps_strcpy(d,end);
				d+=elen;
				break;
			default:
				*d=*s;
				d++;
		}
	}
	*d='\0';
	return(res);
}

enum {
  DPS_VAR_ALIGN_LEFT  = 0,
  DPS_VAR_ALIGN_RIGHT = 1
};

size_t DpsPrintTextTemplate(DPS_AGENT *A, DPS_OUTPUTFUNCTION dps_out, void * stream, char * dst, size_t dst_len, 
				DPS_TEMPLATE *tmplt, const char * templ) {
        DPS_VARLIST *vars = tmplt->Env_Vars;
	DPS_CONV bc_bc, bc_bc_txt, bc_vc, *cnv = &bc_bc;
	DPS_CHARSET *vcs = NULL;
	const char * s;
	char *newvalue = NULL;
	size_t dlen=0;
	int align = DPS_VAR_ALIGN_LEFT;

	DpsConvInit(&bc_bc, A->Conf->bcs, A->Conf->bcs, A->Conf->CharsToEscape, DPS_RECODE_HTML);
	DpsConvInit(&bc_bc_txt, A->Conf->bcs, A->Conf->bcs, A->Conf->CharsToEscape, DPS_RECODE_TEXT);
	
	for(s=templ; (*s) && ((stream) || (dlen < dst_len)); s++){
		int type = 0;
		char *value = NULL, *eval = NULL, *eval2 = NULL;
		char empty[] = "";
		size_t maxlen = 0;
		size_t curlen = 0, charlen;

		if(*s=='$'){
			const char * vbeg=NULL, * vend;
			int pcount=0;

			vcs = NULL;
			align = DPS_VAR_ALIGN_LEFT;
			if(!strncmp(s,"$&(",3)){vbeg=s+3;type='&';}
			else    if(!strncmp(s,"$(",2)){vbeg=s+2;type='(';}
			else	if(!strncmp(s,"$%(",3)){vbeg=s+3;type='%';}
			else	if(!strncmp(s,"$^(",3)){vbeg=s+3;type='^';}
			else	if(!strncmp(s,"$*(",3)){vbeg=s+3;type='*';}
			
			for (vend=s; vend[0]; vend++){
				if(vend[0]=='(')pcount++;
				if(vend[0]==')' && (--pcount)==0)
					break;
			}
			
			if((type)&&(vend)){
				DPS_VAR * var;
				size_t len;
				char name[100]="";
				char *sem, *sem2, *sem3, *cs_name;
				
				len=(vend-vbeg);
				if(len>=sizeof(name))len=sizeof(name)-1;
				dps_strncpy(name,vbeg,len);name[len]='\0';
				if((sem=strchr(name,':'))){
					*sem = '\0';
					if ((sem2 = strchr(sem + 1, ':'))) {
					  *sem2 = '\0';
					  if ((sem3 = strchr(sem2 + 1, ':'))) {
					    *sem3 = '\0';
					    if (isalpha(sem3[1])) {
					      cs_name = DpsCharsetCanonicalName(sem3 + 1);
					      if (cs_name != NULL) vcs = DpsGetCharSet(cs_name);
					      else if (!strcasecmp(sem3 + 1, "right")) align = DPS_VAR_ALIGN_RIGHT;
					    } else {
					      maxlen = DPS_ATOI(sem2 + 1);
					    }
					  }
					  

					  if (isalpha(sem2[1])) {
					    cs_name = DpsCharsetCanonicalName(sem2 + 1);
					    if (cs_name != NULL) vcs = DpsGetCharSet(cs_name);
					    else if (!strcasecmp(sem2 + 1, "right")) align = DPS_VAR_ALIGN_RIGHT;
					  } else {
					    maxlen = DPS_ATOI(sem2 + 1);
					  }
					}
					if (isalpha(sem[1])) {
					  cs_name = DpsCharsetCanonicalName(sem + 1);
					  if (cs_name != NULL) vcs = DpsGetCharSet(cs_name);
					  else if (!strcasecmp(sem + 1, "right")) align = DPS_VAR_ALIGN_RIGHT;
					} else {
					  maxlen = DPS_ATOI(sem + 1);
					}
				}
				
				if ((A->doccount == 0) && !strcasecmp(name, "ndocs")) {
				  DpsURLAction(A, NULL, DPS_URL_ACTION_DOCCOUNT);
				  DpsVarListReplaceInt(vars, "ndocs", A->doccount);
				}

				if((var=DpsVarListFind(vars,name))){
				  
				        switch(type) {
					case '*':
					case '%':
					case '&': value = var->val; 
					  if (vcs != NULL) {
					    DpsConvInit(&bc_vc, A->Conf->bcs, vcs, A->Conf->CharsToEscape, DPS_RECODE_HTML);
					    cnv = &bc_vc;
					  } else cnv = &bc_bc; 
					  break;
					default:  value = (var->txt_val) ? var->txt_val : var->val; 
					  if (vcs != NULL) {
					    DpsConvInit(&bc_vc, A->Conf->bcs, vcs, A->Conf->CharsToEscape, DPS_RECODE_TEXT);
					    cnv = &bc_vc;
					  } else cnv = &bc_bc_txt;
					}
					if(!value)value=empty;
				}else{	
					value=empty;
				}
				
				s=vend;
			}else{
				type=0;
			}
		}
		if(!value)value=empty;

		curlen = DpsUniConvLength(cnv, value); charlen = dps_strlen(value);
		
		if((curlen > maxlen) && (maxlen > 0)) {
		  if ((newvalue = (char*)DpsRealloc(newvalue, 23 * curlen /*maxlen*/ + 23))) {
			  char *c2, *c3;
			  if (align == DPS_VAR_ALIGN_RIGHT) {
			    DpsNConv(cnv, curlen - maxlen, newvalue, 23 * curlen, value, charlen);
			    dps_strcpy(newvalue, "...");
			    DpsNConv(cnv, maxlen + 1, newvalue + 3, 23 * curlen + 1, value + cnv->ibytes, charlen - cnv->ibytes);
			    newvalue[3 + cnv->obytes] = '\0';
			    c2 = strrchr(newvalue, '\2');
			    c3 = strrchr(newvalue, '\3');
			    if ((c2 != NULL) && ((c3 == NULL) || (c2 > c3))) {
			      dps_strcpy(newvalue + 3 + cnv->obytes, "\3");
			    }
			  } else { /* DPS_VAR_ALIGN_LEFT by default */
			    DpsNConv(cnv, maxlen, newvalue, 23 * maxlen + 1, value, charlen);
			    newvalue[cnv->obytes] = '\0';
			    c2 = strrchr(newvalue, '\2');
			    c3 = strrchr(newvalue, '\3');
			    if ((c2 != NULL) && ((c3 == NULL) || (c2 > c3))) {
			      dps_strcpy(newvalue + cnv->obytes, "\3...");
			    } else {
			      dps_strcpy(newvalue + cnv->obytes, "...");
			    }
			  }
			  value = newvalue;
		  }
		} else if ((newvalue = (char*)DpsRealloc(newvalue, 23 * curlen + 23))) {
			  char *c2, *c3;
			  DpsConv(cnv, newvalue, 23 * curlen + 1, value, charlen + 1);
			  newvalue[cnv->obytes] = '\0';
			  c2 = strrchr(newvalue, '\2');
			  c3 = strrchr(newvalue, '\3');
			  if ((c2 != NULL) && ((c3 == NULL) || (c2 > c3))) {
			    dps_strcpy(newvalue + cnv->obytes, "\3...");
			  }
			  value = newvalue;
		}
		switch(type){
			case '(': 
		        case '*':
				eval = DpsRemoveHiLightDup(value);
				if (eval == NULL) break;
				dlen+=out_string(dps_out, stream, dst + dlen, dst_len - dlen, eval);
				DPS_FREE(eval);
				break;
			case '&':
			case '^':
				eval = HiLightDup(value, tmplt->HlBeg, tmplt->HlEnd);
				if (eval == NULL) break;
				dlen+=out_string(dps_out, stream, dst + dlen, dst_len - dlen, eval);
				DPS_FREE(eval);
				break;
			case '%':
				eval = DpsRemoveHiLightDup(value);
				if (eval == NULL) break;
				eval2 =( char*)DpsMalloc(dps_strlen(eval) * 3 + 1);
				if (eval2 != NULL) {
				  DpsEscapeURL(eval2, eval);
				  dlen += out_string(dps_out, stream, dst + dlen, dst_len - dlen, eval2);
				}
				DPS_FREE(eval); DPS_FREE(eval2);
				break;
			default:	/* One character */
				if((stream)&&(*s)) dps_out(stream, "%c", *s);
				if(dst){
					dst[dlen]=*s;
					dlen++;
					dst[dlen]='\0';
				}
		}
	}
	DPS_FREE(newvalue);
	return dlen;
}

static char * GetHtmlTok(const char * src,const char ** lt){
	char * res;
	size_t len;
	if((!src)&&!(src=*lt))return(NULL);
	if(*src=='<'){
		/* Find ">" and skip "<" */
		if((*lt=strchr(src,'>')))(*lt)++;
	}else{
		/* Find tag beginning */
		*lt=strchr(src,'<');
	}
	if(!(*lt)){
		/* Last token */
		res = (char*)DpsStrdup(src);
	}else{
		/* Token in the middle */
		len=(*lt)-src;
		res = (char*)DpsMalloc(len + 2);
		if (res == NULL) return NULL;
		dps_strncpy(res,src,len);
		res[len]='\0';
	}
	return(res);
}

/* 
	FIXME: add support to include into buffer
	FIXME: currently stream only is supported
*/

static void TemplateInclude(DPS_AGENT *Agent, DPS_OUTPUTFUNCTION dps_out, void *stream, DPS_TEMPLATE *tmplt, const char *tok) {
	DPS_HTMLTOK ltag, *tag = &ltag;
	DPS_VARLIST *vars = tmplt->Env_Vars;
	const char *last;
	char *tag_content = NULL;
	DPS_DOCUMENT * Inc=DpsDocInit(NULL);
	size_t i;
	
	size_t max_doc_size = (size_t)DpsVarListFindInt(vars, "MaxDocSize", DPS_MAXDOCSIZE);

	if(!Inc->Buf.buf) Inc->Buf.buf = (char*)DpsMalloc(DPS_NET_BUF_SIZE + 1);
	if (Inc->Buf.buf == NULL) return;
	Inc->Buf.max_size = max_doc_size;
	Inc->Buf.allocated_size = DPS_NET_BUF_SIZE;

	DpsHTMLTOKInit(tag);
	DpsHTMLToken(tok, &last, tag);
	for(i = 0; i < ltag.ntoks; i++) {
		if (ISTAG(i, "content")) {
			tag_content = DpsStrndup(ltag.toks[i].val, ltag.toks[i].vlen);
			break;
		}
	}
	if(tag_content){
		const char *ce;
		size_t vurlen = 256 + 4 * dps_strlen(tag_content);
		char  *vurl = (char*)DpsMalloc(vurlen);

		if (vurl == NULL) return;
		DpsPrintTextTemplate(Agent, dps_out, NULL, vurl, vurlen, tmplt, tag_content);
		DpsURLParse(&Inc->CurURL,vurl);
		DPS_FREE(vurl);
		DpsVarListReplaceStr(&Inc->RequestHeaders, "Host", DPS_NULL2EMPTY(Inc->CurURL.hostname));
		Inc->connp.hostname = (char*)DpsStrdup(DPS_NULL2EMPTY(Inc->CurURL.hostname));
		Inc->connp.port = Inc->CurURL.port ? Inc->CurURL.port : Inc->CurURL.default_port;
		Inc->connp.charset_id = (Agent->Conf->bcs != NULL) ? Agent->Conf->bcs->id : 0;
		DpsSpiderParamInit(&Inc->Spider);
		Inc->connp.timeout = Inc->Spider.read_timeout;
		
		if(DpsHostLookup(Agent, &Inc->connp)) {
		}
					
		if(DpsGetURL(Agent, Inc, NULL) == DPS_OK) {
/*			DpsParseHTTPResponse(Agent,Inc);*/
			if(Inc->Buf.content){
				ce=DpsVarListFindStr(&Inc->Sections,"Content-Encoding","");
#ifdef HAVE_ZLIB
				if(!strcasecmp(ce,"gzip") || !strcasecmp(ce,"x-gzip")){
				  DpsUnGzip(Agent, Inc);
				}else
				if(!strcasecmp(ce,"deflate")){
				  DpsInflate(Agent, Inc);
				}else
				if(!strcasecmp(ce,"compress") || !strcasecmp(ce,"x-compress")){
				  DpsUncompress(Agent, Inc);
				}
#endif
				if(stream){
					dps_out(stream, "%s", Inc->Buf.content);
				}else{
					/* FIXME: add printing to string */
				}
			}
		}
		DPS_FREE(tag_content);
	}
	DpsDocFree(Inc);
}

static size_t TemplateTag(DPS_AGENT *Agent, DPS_OUTPUTFUNCTION dps_out, void *stream, char *dst, size_t dst_len,
			  DPS_TEMPLATE *tmplt, const char *tok, int checked) {
	char * opt;
	DPS_HTMLTOK ltag, *tag = &ltag;
	DPS_VARLIST *vars = tmplt->Env_Vars;
	const char *last;
	DPS_VAR * var=NULL;
	char * vname = NULL, *value = NULL;
	size_t i, res = 0;
	
	opt = (char*)DpsMalloc(dps_strlen(tok) + 200);
	if (opt == NULL) return DPS_ERROR;
	DpsHTMLTOKInit(tag);
	DpsHTMLToken(tok, &last, tag);
	sprintf(opt, "<");

	for (i = 0; i < ltag.ntoks; i++) {
		if (ISTAG(i, "selected")) {
			vname = DpsStrndup(ltag.toks[i].val, ltag.toks[i].vlen);
		} else if (ISTAG(i, "checked")) {
		        vname = DpsStrndup(ltag.toks[i].val, ltag.toks[i].vlen);
		} else if (ISTAG(i, "value")) {
			value = DpsStrndup(ltag.toks[i].val, ltag.toks[i].vlen);
			sprintf(DPS_STREND(opt), "value=\"%s\" ", value);
		} else if (ISTAG(i, "/")) { /* the closing slash is necessary for some xhtml tags */
			sprintf(DPS_STREND(opt), " /");
		} else {
			char *tname = DpsStrndup(ltag.toks[i].name, ltag.toks[i].nlen);
			if (ltag.toks[i].vlen) {
				char *tval = DpsStrndup(ltag.toks[i].val, ltag.toks[i].vlen);
				sprintf(DPS_STREND(opt), "%s=\"%s\" ", tname, tval);
				DPS_FREE(tval);
			} else {
	 			sprintf(DPS_STREND(opt), "%s ", tname);
	 		}
	 		DPS_FREE(tname);
		}
	}

	if(vname) {
		var = DpsVarListFindWithValue(vars, DpsTrim(vname, "$&%^()"), value ? value:"");
	}

	sprintf(DPS_STREND(opt), "%s%s%s>", var ? (checked ? "checked" : "selected") : "",
		var ? "=" : "",
		var ? (checked ? "\"checked\"" : "\"selected\"") : ""
		);

	if (vname) { DPS_FREE(vname); }
	if (value) { DPS_FREE(value); }

	res = DpsPrintTextTemplate(Agent, dps_out, stream, dst, dst_len, tmplt, opt);
	DPS_FREE(opt);
	return res;
}

#define DPS_IFSTACKMAX	15

typedef struct dps_if_stack_item_st {
	int	condition;
	int	showelse;
} DPS_IFITEM;

typedef struct dps_if_stack_st {
	size_t		pos;
	DPS_IFITEM	Items[DPS_IFSTACKMAX+1];
} DPS_IFSTACK;

static void DpsIfStackInit(DPS_IFSTACK *S){
	DPS_IFITEM	*top=&S->Items[0];
	
	bzero((void*)S, sizeof(*S));
	top->condition=1;
	top->showelse=1;
}

static DPS_IFITEM *DpsIfStackPush(DPS_IFSTACK *S){
	if(S->pos<DPS_IFSTACKMAX){
		DPS_IFITEM 	*cur=&S->Items[S->pos+1];
		DPS_IFITEM	*prev=&S->Items[S->pos];
		
		S->pos++;
		cur->condition=prev->condition;
		cur->showelse=prev->condition;
	}
	return &S->Items[S->pos];
}

static DPS_IFITEM *DpsIfStackPop(DPS_IFSTACK *S){
	if(S->pos>0)S->pos--;
	return &S->Items[S->pos];
}

static void HTMLTokToVarList(DPS_VARLIST *vars,DPS_HTMLTOK *tag){
	size_t	toks;
	
	for(toks=0;toks<tag->ntoks;toks++){
		char *vr=tag->toks[toks].name ? DpsStrndup(tag->toks[toks].name,tag->toks[toks].nlen) : (char*)DpsStrdup("");
		char *vl=tag->toks[toks].val ? DpsStrndup(tag->toks[toks].val,tag->toks[toks].vlen) : (char*)DpsStrdup("");
		DpsVarListReplaceStr(vars,vr,vl);
		DPS_FREE(vr);
		DPS_FREE(vl);
	}
}

static void TemplateSet(DPS_AGENT *Agent,DPS_VARLIST *vars,const char *tok,DPS_IFSTACK *is){
	DPS_HTMLTOK	tag;
	DPS_VARLIST	attr;
	const char	*var,*val,*hlast=NULL;
	DPS_IFITEM	*it=&is->Items[is->pos];
	
	if (it->condition == 0) return;
	
	DpsHTMLTOKInit(&tag);
	DpsHTMLToken(tok,&hlast,&tag);
	DpsVarListInit(&attr);
	HTMLTokToVarList(&attr,&tag);
	
	var=DpsVarListFindStr(&attr,"Name","");
	val=DpsVarListFindStr(&attr,"Content","");
	DpsVarListReplaceStr(vars,var,val);

	if (strncasecmp(var, "ENV.", 4) == 0) {
	  setenv(var+4, val, 1);
	}
	
	DpsVarListFree(&attr);
}

static void TemplateCopy(DPS_AGENT *Agent,DPS_VARLIST *vars,const char *tok,DPS_IFSTACK *is){
	DPS_HTMLTOK	tag;
	DPS_VARLIST	attr;
	const char	*var,*val,*hlast=NULL;
	DPS_IFITEM	*it=&is->Items[is->pos];

	if (it->condition == 0) return;
	
	DpsHTMLTOKInit(&tag);
	DpsHTMLToken(tok,&hlast,&tag);
	DpsVarListInit(&attr);
	HTMLTokToVarList(&attr,&tag);
	
	var=DpsVarListFindStr(&attr,"Name","");
	val=DpsVarListFindStr(&attr,"Content","");
	val=DpsVarListFindStr(vars,val,"");
	DpsVarListReplaceStr(vars,var,val);
	
	DpsVarListFree(&attr);
}


static void TemplateCondition(DPS_AGENT *Agent,DPS_VARLIST *vars,const char *tok,DPS_IFSTACK *is){
	DPS_HTMLTOK	tag;
	DPS_VARLIST	attr;
	const char	*var,*val,*hlast=NULL;
	DPS_IFITEM	*it=&is->Items[is->pos];
	
	DpsHTMLTOKInit(&tag);
	DpsHTMLToken(tok,&hlast,&tag);
	DpsVarListInit(&attr);
	
	HTMLTokToVarList(&attr,&tag);
	
	var=DpsVarListFindStr(&attr,"Name","");
	val=DpsVarListFindStr(&attr,"Content","");
	var=DpsVarListFindStr(vars,var,"");

	if(!(strncasecmp(tok,"<!IFNOT",7)) || !(strncasecmp(tok, "<!ELSEIFNOT", 11)) || !(strncasecmp(tok, "<!ELIFNOT", 9)) ) {
		it->condition = strcasecmp(var,val);
	}else
	if( !(strncasecmp(tok, "<!IFLIKE", 8)) || !(strncasecmp(tok, "<!ELIKE", 7)) || !(strncasecmp(tok, "<!ELSELIKE", 10)) ) {
		it->condition = !DpsWildCaseCmp(var, val);
	}else
	if( !(strncasecmp(tok, "<!IF", 4)) || !(strncasecmp(tok, "<!ELIF", 6)) || !(strncasecmp(tok, "<!ELSEIF", 8)) ) {
		it->condition = !strcasecmp(var,val);
	}
	
	DpsVarListFree(&attr);
}

static void TemplateIf(DPS_AGENT *Agent,DPS_VARLIST *vars,const char *tok,DPS_IFSTACK *is){
	DPS_IFITEM	*it=DpsIfStackPush(is);
	
	if(it->condition){
		TemplateCondition(Agent,vars,tok,is);
		if(it->condition)it->showelse=0;
	}
}

static void TemplateEndIf(DPS_AGENT *Agent,DPS_VARLIST *vars,const char *tok,DPS_IFSTACK *is){
	DpsIfStackPop(is);
}

static void TemplateElseIf(DPS_AGENT *Agent,DPS_VARLIST *vars,const char *tok,DPS_IFSTACK *is){
	DPS_IFITEM	*it=&is->Items[is->pos];
	
	if(it->showelse){
		TemplateCondition(Agent,vars,tok,is);
		if(it->condition)it->showelse=0;
	}else{
		it->condition=0;
	}
}

static void TemplateElse(DPS_AGENT *Agent,DPS_VARLIST *vars,const char *tok,DPS_IFSTACK *is){
	DPS_IFITEM	*it=&is->Items[is->pos];
	it->condition=it->showelse;
}



static void PrintHtmlTemplate(DPS_AGENT * Agent, DPS_OUTPUTFUNCTION dps_out, void * stream, char * dst, size_t dst_len, 
			      DPS_TEMPLATE *tmplt, const char * template) {
	const char	*lt;
	char		*tok;
	size_t		dlen=0;
	DPS_IFSTACK	is;
	DPS_VARLIST     *vars = tmplt->Env_Vars;
	
	DpsIfStackInit(&is);
	
	tok=GetHtmlTok(template,&lt);
	while(tok){
		if(!(strncasecmp(tok,"<!SET",5))){
		        TemplateSet(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok,"<!COPY",6))){
			TemplateCopy(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok,"<!IF",4))){
			TemplateIf(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok,"<!IFLIKE", 8))) {
			TemplateIf(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok, "<!ELSEIF", 8))) {
			TemplateElseIf(Agent, vars, tok, &is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok,"<!ELIF",6))){
			TemplateElseIf(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok, "<!ELIKE", 7))) {
			TemplateElseIf(Agent, vars, tok, &is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok, "<!ELSELIKE", 10))) {
			TemplateElseIf(Agent, vars, tok, &is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok,"<!ELSE",6))){
			TemplateElse(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok,"<!ENDIF",7))){
			TemplateEndIf(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(!(strncasecmp(tok,"<!/IF",5))){
			TemplateEndIf(Agent,vars,tok,&is); if (*lt == NL_CHAR) lt++;
		}else
		if(is.Items[is.pos].condition){
			if(!(strncasecmp(tok,"<OPTION",7))){
			  dlen += TemplateTag(Agent, dps_out, stream, dst+dlen, dst_len-dlen, tmplt, tok, 0);
			}else
			if(!(strncasecmp(tok,"<INPUT",6))){
			  dlen += TemplateTag(Agent, dps_out, stream, dst+dlen, dst_len-dlen, tmplt, tok, 1);
			}else
			if(!strncasecmp(tok,"<!INCLUDE",9)){
				if(Agent)TemplateInclude(Agent, dps_out, stream, tmplt, tok);
			}else{
				dlen += DpsPrintTextTemplate(Agent, dps_out, stream, dst + dlen, dst_len - dlen, tmplt, tok);
			}
		}
		DPS_FREE(tok);
		tok=GetHtmlTok(NULL,&lt);
	}
}


void DpsTemplateFree(DPS_TEMPLATE *tmplt) {
  DpsVarListFree(&tmplt->vars);
  DPS_FREE(tmplt->HlBeg);
  DPS_FREE(tmplt->HlEnd);
  DPS_FREE(tmplt->GrBeg);
  DPS_FREE(tmplt->GrEnd);
}

void __DPSCALL DpsTemplatePrint(DPS_AGENT * Agent, DPS_OUTPUTFUNCTION dps_out, void *stream, char *dst, size_t dst_len, 
				DPS_TEMPLATE *tmplt, const char *w) {
	DPS_VAR	*First=NULL;
	DPS_VARLIST *vars = tmplt->Env_Vars;
	DPS_VARLIST *tm = &tmplt->vars;
	size_t	t, r;
	size_t	matches=0;
	size_t	format=(size_t)DpsVarListFindInt(vars,"o",0);
	
	if(dst)*dst='\0';
	for (r = 0; r < 256; r++)
	for(t = 0; t < tm->Root[r].nvars; t++) {
		if(!strcasecmp(w, tm->Root[r].Var[t].name)){
			if(!First)First = &tm->Root[r].Var[t];
			if(matches==format){
				PrintHtmlTemplate(Agent, dps_out, stream, dst, dst_len, tmplt, tm->Root[r].Var[t].val);
				return;
			}
			matches++;
		}
	}
	if (First) PrintHtmlTemplate(Agent, dps_out, stream, dst, dst_len, tmplt, First->val);
	return;
}

static int ParseVariable(DPS_AGENT *Agent, DPS_ENV *Env, DPS_VARLIST *vars, char *p_str) {
  char *tok,*lt, *cmd = NULL;
  char *str = DpsTrim(p_str, " \t\r\n");
  int res = DPS_OK;
	
/*	if((tok = dps_strtok_r(str, " \t\r\n", &lt))) {*/
		char *arg = NULL;
		char *p = strchr(str, ' ');
		if (p == NULL) cmd = DpsStrdup(str);
		else cmd = DpsStrndup(str, p - str);
					
		if(!strcasecmp(cmd, "DBAddr")) {
			char *pp;
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  if((arg = dps_strtok_r(NULL, " \t\r\n", &lt)) &&
			     (pp = DpsParseEnvVar(Env, arg))) {

			    if (DPS_OK!=DpsDBListAdd(&Env->dbl, pp, DPS_OPEN_MODE_READ)) {
			      sprintf(Env->errstr,"Invalid DBAddr: '%s'", (!pp)?"NULL":pp);
			      DPS_FREE(cmd);
			      return DPS_ERROR;
			    }
/*			  DpsVarListReplaceStr(vars,tok,arg);*/
			    DpsFree(pp);
			  }
		}else
		if(!strcasecmp(cmd, "ImportEnv")){
                        const char *val;
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  if ((arg = dps_strtok_r(NULL, " \t\r\n", &lt)) &&
			      ((val = getenv(arg))) )
                                DpsVarListReplaceStr(vars, arg, val);
                }else
		if((str[0]=='R'||str[0]=='r')&&(str[1]>='0')&&(str[1]<='9')){
			float r;
			int ir;
			arg = NULL;
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " =\t\r\n", &lt); 
			if(arg){
				r = (float)DPS_ATOF(arg);
				srand((unsigned)time(0));
				r = r * rand() / RAND_MAX; ir = (int)r;
				DpsVarListReplaceInt(vars,str,ir);
			}
		}else
		  if(!strncasecmp(str, "HlBeg", 5)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars,"HlBeg", DPS_NULL2EMPTY(lt));
		}else
		    if(!strncasecmp(str, "HlEnd", 5)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars,"HlEnd", DPS_NULL2EMPTY(lt));
		}else
		      if(!strncasecmp(str, "GrBeg", 5)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars,"GrBeg", DPS_NULL2EMPTY(lt));
		}else
			if(!strncasecmp(str, "GrEnd", 5)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars,"GrEnd", DPS_NULL2EMPTY(lt));
		}else
			  if(!strncasecmp(str, "DateFormat", 10)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, "DateFormat", DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "PagesPerScreen", 14)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "Log2stderr", 10)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "StoredocURL", 11)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "Locale", 6)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "DetectClones", 12)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strcasecmp(cmd, "sp")) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strcasecmp(cmd, "sy")) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "ResultContentType", 17)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "CatColumns", 10)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "empty", 5)) {
		  if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
		    DpsVarListReplaceStr(vars, tok, DPS_NULL2EMPTY(lt));
		}else
		if(!strncasecmp(str, "Limit", 5)){
			char * sc, * nm;
			arg = NULL;
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			if((sc = strchr(arg, ':'))) {
				*sc='\0'; sc++;
				nm = (char*)DpsMalloc(dps_strlen(arg) + 8);
				if (nm == NULL) { DPS_FREE(cmd); return DPS_ERROR; }
				sprintf(nm, "Limit-%s", arg);
				DpsVarListReplaceStr(vars, nm, sc);
				DPS_FREE(nm);
			}
		}else
		  if(!strncasecmp(str, "LocalCharset", 12)) {
		        if((tok = dps_strtok_r(str, " \t\r\n", &lt))) {
			  if ((arg = dps_strtok_r(NULL, " \t\r\n", &lt))) {
			    DpsVarListReplaceStr(vars, tok, arg);
			    Env->lcs = DpsGetCharSet(arg);
			  }
			}
                }else
		    if(!strncasecmp(str, "BrowserCharset", 14)) {
		        if((tok = dps_strtok_r(str, " \t\r\n", &lt))) {
			  if ((arg = dps_strtok_r(NULL, " \t\r\n", &lt))) {
			    DpsVarListReplaceStr(vars, tok, arg);
			    Env->bcs = DpsGetCharSet(arg);
			  }
			}
                }else
		if(!strncasecmp(str, "ExcerptSize", 11)) {
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			if (arg) DpsVarListReplaceInt(vars, tok, atoi(arg));
		}else
		if(!strncasecmp(str, "ExcerptPadding", 14)) {
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			if (arg) DpsVarListReplaceInt(vars, tok, atoi(arg));
		}else
		if(!strncasecmp(str, "ps", 2)) {
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			if (arg) DpsVarListReplaceInt(vars, tok, atoi(arg));
		}else
		if(!strncasecmp(str, "geo.zoom", 8)) {
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			if (arg) DpsVarListReplaceInt(vars, tok, atoi(arg));
		}else
		if(!strncasecmp(str, "my", 2)) {
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			if (arg) DpsVarListReplaceStr(vars, tok, arg);
		}else
		  if((str[0] == 'm' || str[0] == 'M') && (str[1] == ' ' || str[1] == HT_CHAR)) {
			if((tok = dps_strtok_r(str, " \t\r\n", &lt)))
			  arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			if (arg) DpsVarListReplaceStr(vars, tok, arg);
		}else{
		 
		  res = DPS_ERROR;
/*			arg = dps_strtok_r(NULL, " \t\r\n", &lt);
			DpsVarListReplaceStr(vars,tok,arg);*/
		}
/*	}*/
		DPS_FREE(cmd);
	return res;
}

/* Load template  */
int DpsTemplateLoad(DPS_AGENT *Agent, DPS_ENV * Env, DPS_TEMPLATE *t, const char * tname) {
	struct stat     sb;
	DPS_CFG         Cfg;
	DPS_SERVER      Srv;
	DPS_IFSTACK     is;
	DPS_VARLIST     *vars = t->Env_Vars;
	DPS_VARLIST     *tmpl = &t->vars;
	DPS_CHARSET     *unics;
#ifdef HAVE_ASPELL
	DPS_CHARSET     *utfcs;
#endif
	char		*str;
	const char	*dbaddr = NULL;
	char		*cur = NULL, *data = NULL, *cur_n = NULL;
	int		variables = 0;
	int             i, fd;
	char		cursection[128]="";
	char		nameletter[]=
				"abcdefghijklmnopqrstuvwxyz"
				"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
				"0123456789._";
	char            savebyte;
	
	DpsServerInit(&Srv);
	DpsIfStackInit(&is);
	bzero((void*)&Cfg, sizeof(Cfg));
	Cfg.Indexer = Agent;
	/*Agent->Conf->Cfg_Srv =*/ Cfg.Srv = &Srv;
	Cfg.flags = DPS_FLAG_SPELL | DPS_FLAG_ADD_SERV;
	Cfg.level = 0;

	if (stat(tname, &sb)) {
	  dps_snprintf(Env->errstr, sizeof(Env->errstr)-1, "Unable to stat template '%s': %s", tname, strerror(errno));
	  DpsServerFree(&Srv);
	  return 1;
	}
	for (i = 0; (i < 5) && ((fd = DpsOpen2(tname, O_RDONLY)) <= 0); i++); 
	if (fd <= 0) {
	  dps_snprintf(Env->errstr, sizeof(Env->errstr)-1, "Unable to open template '%s': %s", tname, strerror(errno));
	  DpsServerFree(&Srv);
	  return 1;
	}
	if ((data = (char*)DpsMalloc((size_t)sb.st_size + 1)) == NULL) {
	  dps_snprintf(Env->errstr, sizeof(Env->errstr)-1, "Unable to alloc %d bytes", sb.st_size);
	  DpsClose(fd);
	  DpsServerFree(&Srv);
	  return 1;
	}
	if (read(fd, data, (size_t)sb.st_size) != (ssize_t)sb.st_size) {
	  dps_snprintf(Env->errstr, sizeof(Env->errstr)-1, "Unable to read template '%s': %s", tname, strerror(errno));
	  DPS_FREE(data);
	  DpsClose(fd);
	  DpsServerFree(&Srv);
	  return 1;
	}
	data[sb.st_size] = '\0';
	str = data;
	cur_n = strchr(str, NL_INT);
	if (cur_n != NULL) {
	  cur_n++;
	  savebyte = *cur_n;
	  *cur_n = '\0';
	}

	while(str != NULL) {
		char	*s;

		s = DpsTrim(str," \t\r\n");

		if (*s == '\0') goto pre_loop_continue;
		
		if(!strcasecmp(s,"<!--variables")){
			variables=1;
			goto loop_continue;
		}
		
		if(!strcmp(s,"-->") && variables){
			variables=0;
			goto loop_continue;
		}
		
		if(variables){
			int r;
			if(!*s) goto loop_continue;
			if(*s=='#') goto loop_continue;
			
			if(!(strncasecmp(s, "<!SET", 5))) {
			  TemplateSet(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!COPY", 6))) {
			  TemplateCopy(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!IF", 4))) {
			  TemplateIf(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!IFLIKE", 8))) {
			  TemplateIf(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!ELSEIF", 8))) {
			  TemplateElseIf(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!ELIF", 6))) {
			  TemplateElseIf(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!ELIKE", 7))) {
			  TemplateElseIf(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!ELSELIKE", 10))) {
			  TemplateElseIf(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!ELSE", 6))) {
			  TemplateElse(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!ENDIF", 7))) {
			  TemplateEndIf(Agent, vars, s, &is);
			}else
			if(!(strncasecmp(s, "<!/IF", 5))) {
			  TemplateEndIf(Agent, vars, s, &is);
			}else
			if(is.Items[is.pos].condition){
			  if(DPS_OK != (r = ParseVariable(Agent, Env, vars, s)) && DPS_OK != ( r = DpsEnvAddLine(&Cfg,s))) {
			    DpsServerFree(&Srv);
			    return r;
			  }
			}
			goto loop_continue;
		}
		
		if(!memcmp(s,"<!--",4)){
			char *e;
			
			for(e=s+4;(*e)&&(strchr(nameletter,*e)||(*e=='/'));e++);
			
			if(!strcmp(e,"-->")){
				*e='\0';
				s+=4;
				
				if(s[0]=='/'){
					if(!strcasecmp(s+1,cursection) && cursection[0]){
					  if (cur) {
					    size_t seclen = dps_strlen(cur);
					    if (cur[seclen - 1] == NL_CHAR) cur[seclen - 1] = '\0';
					  }
						DpsVarListReplaceStr(tmpl, cursection, cur ? cur : "");
						cursection[0]='\0';
						DPS_FREE(cur);
						goto loop_continue;
					}
				}else
				if(s[1]){
					dps_strncpy(cursection,s,sizeof(cursection));
					cursection[sizeof(cursection)-1]='\0';
					goto loop_continue;
				}
			}
		}
		
	pre_loop_continue:
		if(!cursection[0])
			goto loop_continue;
		
		if(!cur){
		        size_t s_len;
			cur = (char*)DpsRealloc(cur, (s_len = dps_strlen(str)) + 3);
			if (cur == NULL) goto loop_continue;
			dps_strcpy(cur, str); dps_strcpy(cur + s_len, "\n"); /*dps_strcat(cur + len, "\n");*/
/*			cur = (char*)DpsStrdup(str);*/
		}else{
		        size_t len, s_len;
			cur = (char*)DpsRealloc(cur, (len = dps_strlen(cur)) + (s_len = dps_strlen(str)) + 3);
			if (cur == NULL) goto loop_continue;
			dps_strcpy(cur + len, str); dps_strcpy(cur + len + s_len, "\n"); /*dps_strcat(cur + len, "\n");*/
/*			sprintf(cur + len, "%s\n", str);*/
		}
	loop_continue:
		str = cur_n;
		if (str != NULL) {
		  *str = savebyte;
		  cur_n = strchr(str, NL_INT);
		  if (cur_n != NULL) {
		    cur_n++;
		    savebyte = *cur_n;
		    *cur_n = '\0';
		  }
		}
		
	}
	DpsClose(fd);
	DPS_FREE(cur);
	DPS_FREE(data);
	
	if(Env->Spells.nspell) {
		DpsSortDictionary(&Env->Spells);
		DpsSortAffixes(&Env->Affixes, &Env->Spells);
		DpsSortQuffixes(&Env->Quffixes, &Env->Spells);
	}
	DpsSynonymListSort(&Env->Synonyms);
	DpsAcronymListSort(&Env->Acronyms);
	
	DPS_FREE(t->HlBeg);
	DPS_FREE(t->HlEnd);
	t->HlBeg = DpsStrdup(DpsVarListFindStrTxt(vars, "HlBeg", ""));
	t->HlEnd = DpsStrdup(DpsVarListFindStrTxt(vars, "HlEnd", ""));
	DPS_FREE(t->GrBeg);
	DPS_FREE(t->GrEnd);
	t->GrBeg = DpsStrdup(DpsVarListFindStrTxt(vars, "GrBeg", ""));
	t->GrEnd = DpsStrdup(DpsVarListFindStrTxt(vars, "GrEnd", ""));
	
	unics = DpsGetCharSet("sys-int");
	DpsConvInit(&Agent->uni_lc, unics, Env->lcs, Env->CharsToEscape, DPS_RECODE_HTML);
	DpsConvInit(&Agent->lc_uni, Env->lcs, unics, Env->CharsToEscape, DPS_RECODE_HTML);
	DpsConvInit(&Agent->lc_uni_text, Env->lcs, unics, Env->CharsToEscape, DPS_RECODE_TEXT);
#ifdef HAVE_ASPELL
	utfcs = DpsGetCharSet("UTF-8");
	DpsConvInit(&Agent->utf_uni, utfcs, unics, Env->CharsToEscape, DPS_RECODE_HTML);
	DpsConvInit(&Agent->utf_lc, utfcs, Env->lcs, Env->CharsToEscape, DPS_RECODE_HTML);
	DpsConvInit(&Agent->uni_utf, unics, utfcs, Env->CharsToEscape, DPS_RECODE_HTML);
#endif
	
#ifdef HAVE_SQL
	if(Env->dbl.nitems == 0) {
	  if (DPS_OK!=DpsDBListAdd(&Env->dbl, dbaddr = "mysql://localhost/search", DPS_OPEN_MODE_READ)) {
	    sprintf(Env->errstr,"Invalid DBAddr: '%s'", (!dbaddr)?"NULL":dbaddr);
	    DpsServerFree(&Srv);
	    return DPS_ERROR;
	  }
	}
#endif
	if(Env->dbl.nitems == 0) {
	  if (DPS_OK!=DpsDBListAdd(&Env->dbl, dbaddr =  "searchd://localhost/", DPS_OPEN_MODE_READ)) {
	    sprintf(Env->errstr,"Invalid DBAddr: '%s'", (!dbaddr)?"NULL":dbaddr);
	    DpsServerFree(&Srv);
	    return DPS_ERROR;
	  }
	}

	DpsVarListAddLst(vars, &Srv.Vars, NULL, "Request.*");
	DpsServerFree(&Srv);
	return DPS_OK;
}
