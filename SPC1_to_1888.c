#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <time.h>
#include <pthread.h>

#include "ieee1888.h"

#define MAXIMUM_1888SERVER 3
#define MAXIMUM_SPC1_TAP 10
#define MAXIMUM_STRING_LENGTH 300

#define TAP_KEEP 2
#define TAP_ON   1
#define TAP_OFF  0

#define SMART_TAP_PORT_PE                  1
#define SMART_TAP_PORT_ONOFFStatus         2
#define SMART_TAP_PORT_ONOFFControl        3
#define SMART_TAP_PORT_DefaultONOFFStatus  4
#define SMART_TAP_PORT_DefaultONOFFControl 5

struct smart_tap_port {
  char PE[6];
  time_t PE_time;

  int ONOFFStatus;
  time_t ONOFFStatus_time;

  int ONOFFControl;
  time_t ONOFFControl_time;

  int DefaultONOFFStatus;
  time_t DefaultONOFFStatus_time;

  int DefaultONOFFControl;
  time_t DefaultONOFFControl_time;
};

struct smart_tap {
  char id[10];
  struct smart_tap_port port[4];
};

struct smart_tap m_tap[MAXIMUM_SPC1_TAP];
int n_tap=0;

int xbee_fd;
char xbee_buffer[255];
char* xbee_bufptr;
int xbee_nbytes;

// functions
int find_tap_index_by_id(const char* id);

// -----------------
//  Config Manager 
// -----------------
char ieee1888server[MAXIMUM_1888SERVER][MAXIMUM_STRING_LENGTH];
char ieee1888_hosting_port[MAXIMUM_STRING_LENGTH];
char ieee1888_pointid_host[MAXIMUM_STRING_LENGTH];
char spc1_tap_id[MAXIMUM_SPC1_TAP][MAXIMUM_STRING_LENGTH];

void formatting(char* line, int n){

	int i;
	char* p=line;
	char* q=line;
	for(i=0;i<n;i++,p++){
	   if(0x21<=*p && *p<0x7e){
		*q=*p; q++;
	   }
	   if(0==*p){
	        *q=*p;
	   }
	}
}

int load_config(const char* config_path){

	int i;
	FILE* fp=fopen(config_path,"r");
	if(fp==NULL){
		return -1;
	}

	for(i=0;i<MAXIMUM_1888SERVER;i++){
 		memset(ieee1888server[i],0,MAXIMUM_STRING_LENGTH);
	}
	memset(ieee1888_hosting_port,0,MAXIMUM_STRING_LENGTH);
	memset(ieee1888_pointid_host,0,MAXIMUM_STRING_LENGTH);
	for(i=0;i<MAXIMUM_SPC1_TAP;i++){
		memset(spc1_tap_id[i],0,MAXIMUM_STRING_LENGTH);
	}

	char buf[1000];
	char key[50];

	char* c;
	char* d;
	while(fgets(buf,1000,fp)!=NULL){

		d=strstr(buf,";");

		for(i=0;i<MAXIMUM_1888SERVER;i++){
			sprintf(key,"IEEE1888_SERVER_%d:",i+1);
			c=strstr(buf,key);
			if(c!=NULL && (d==NULL || c<d)){
				strncpy(ieee1888server[i],c+strlen(key),MAXIMUM_STRING_LENGTH);
				formatting(ieee1888server[i],MAXIMUM_STRING_LENGTH);
				break;
			}
		}
		
		sprintf(key,"IEEE1888_HOSTING_PORT:");
		c=strstr(buf,key);
		if(c!=NULL && (d==NULL || c<d)){
			strncpy(ieee1888_hosting_port,c+strlen(key),MAXIMUM_STRING_LENGTH);
			formatting(ieee1888_hosting_port,MAXIMUM_STRING_LENGTH);
		}
		
		sprintf(key,"IEEE1888_POINTID_HOST:");
		c=strstr(buf,key);
		if(c!=NULL && (d==NULL || c<d)){
			strncpy(ieee1888_pointid_host,c+strlen(key),MAXIMUM_STRING_LENGTH);
			formatting(ieee1888_pointid_host,MAXIMUM_STRING_LENGTH);
		}

		for(i=0;i<MAXIMUM_SPC1_TAP;i++){
			sprintf(key,"SPC1_TAP_ID_%d:",i);
			c=strstr(buf,key);
			if(c!=NULL && (d==NULL || c<d)){
				strncpy(spc1_tap_id[i],c+strlen(key),MAXIMUM_STRING_LENGTH);
				formatting(spc1_tap_id[i],MAXIMUM_STRING_LENGTH);
				break;
			}
		}
	}
	return 0;
}

int print_loaded_config(){
	int i;

	for(i=0;i<MAXIMUM_1888SERVER;i++){
 		printf("IEEE1888_SERVER_%d:%s\n",i+1,ieee1888server[i]);
	}
	printf("IEEE1888_HOSTING_PORT:%s\n",ieee1888_hosting_port);
	printf("IEEE1888_POINTID_HOST:%s\n",ieee1888_pointid_host);
	for(i=0;i<MAXIMUM_SPC1_TAP;i++){
		printf("SPC1_TAP_ID_%d:%s\n",i,spc1_tap_id[i]);
	}
	return 0;
}

// ----------------
//  Status Logger
// ----------------

int ieee1888_access_status[MAXIMUM_1888SERVER];
time_t ieee1888_access_time[MAXIMUM_1888SERVER];

void put_status_for_notification(){

	int i;
	int status=0;
	time_t now=time(NULL);

	for(i=0;i<MAXIMUM_1888SERVER;i++){
		if(strlen(ieee1888server[i])>0){

			// if data access is failed over 10minutes (600sec), raise the error flag.
			if(ieee1888_access_time[i]!=0
   			     && ieee1888_access_time[i]<now-600){
				status=1;
			}
		}
	}

	FILE* fp=fopen("/tmp/SPC1-to-1888_status","w");
	if(status){
		fprintf(fp,"Error");
	}else{
		fprintf(fp,"OK");
	}
	fclose(fp);
}

void print_status_in_html(){

	int i,j,k;
	int col=0;
	char buf[100];
	char buf2[100];
	FILE* fp=fopen("/home/www-data/index.html","w");

	if(fp==NULL){
		return ;
	}

	fprintf(fp,"<html>\n");
	fprintf(fp,"<head>\n");
	fprintf(fp,"<title>SPC1-to-IEEE1888 Gateway Status Page</title>\n");
	fprintf(fp,"<style type=text/css>\n");
	fprintf(fp,"#headrow {BACKGROUND-COLOR:#99ffcc; TEXT-ALIGN:center;}\n");
	fprintf(fp,"#evenrow {BACKGROUND-COLOR:#f0f0ff; TEXT-ALIGN:center;}\n");
	fprintf(fp,"#oddrow {BACKGROUND-COLOR:#f8f8ff; TEXT-ALIGN:center;}\n");
	fprintf(fp,"</style>\n");
	fprintf(fp,"</head>\n");
	fprintf(fp,"<body>\n");


	fprintf(fp,"<h1>SPC1-to-IEEE1888 Gateway Status Page</h1>\n");
	fprintf(fp,"<h2>TAP Status</h2>\n");
	fprintf(fp,"<table>\n");
	fprintf(fp,"<tr id=headrow><td>TAP_ID</td><td>Port No.</td><td>Property</td><td>Time</td><td>Value</td></tr>");
	for(i=0;i<MAXIMUM_SPC1_TAP;i++){
          if(strlen(spc1_tap_id[i])==5){
	    int idx=find_tap_index_by_id(spc1_tap_id[i]);
	    if(idx<0 || n_tap<=idx){
	      continue;
	    }
	    for(j=0;j<4;j++){
 	      for(k=0;k<5;k++){
                  char str_prop[20];
		  char str_time[40];
		  char str_value[10];
		  switch(k){
		  case 0: 
		    strcpy(str_prop,"PE");
		    strftime(str_time,40,"%Y-%m-%d %H:%M:%S",localtime(&m_tap[idx].port[j].PE_time));
		    if(m_tap[idx].port[j].PE_time==0){
		      strcpy(str_value,"--");
		    }else{
		      strcpy(str_value,m_tap[idx].port[j].PE);
		    }
	            break;

		  case 1:
		    strcpy(str_prop,"ONOFFStatus");
		    strftime(str_time,40,"%Y-%m-%d %H:%M:%S",localtime(&m_tap[idx].port[j].ONOFFStatus_time));
		    if(m_tap[idx].port[j].ONOFFStatus_time==0){
		      strcpy(str_value,"--");
		    }else{
		      if(m_tap[idx].port[j].ONOFFStatus==1){
		        strcpy(str_value,"ON");
		      }else{
		        strcpy(str_value,"OFF");
		      }
		    }
		    break;

		  case 2:
		    strcpy(str_prop,"ONOFFControl");
		    strftime(str_time,40,"%Y-%m-%d %H:%M:%S",localtime(&m_tap[idx].port[j].ONOFFControl_time));
		    if(m_tap[idx].port[j].ONOFFControl_time==0){
		      strcpy(str_value,"--");
		    }else{
		      if(m_tap[idx].port[j].ONOFFControl==1){
		        strcpy(str_value,"ON");
		      }else{
		        strcpy(str_value,"OFF");
		      }
		    }
		    break;

		  case 3:
		    strcpy(str_prop,"DefaultONOFFStatus");
		    strftime(str_time,40,"%Y-%m-%d %H:%M:%S",localtime(&m_tap[idx].port[j].DefaultONOFFStatus_time));
		    if(m_tap[idx].port[j].DefaultONOFFStatus_time==0){
		      strcpy(str_value,"--");
		    }else{
		      if(m_tap[idx].port[j].DefaultONOFFStatus==2){
		        strcpy(str_value,"KEEP");
		      }else if(m_tap[idx].port[j].DefaultONOFFStatus==1){
		        strcpy(str_value,"ON");
		      }else{
		        strcpy(str_value,"OFF");
		      }
		    }
		    break;
		  
		  case 4:
		    strcpy(str_prop,"DefaultONOFFControl");
		    strftime(str_time,40,"%Y-%m-%d %H:%M:%S",localtime(&m_tap[idx].port[j].DefaultONOFFControl_time));
		    if(m_tap[idx].port[j].DefaultONOFFControl_time==0){
		      strcpy(str_value,"--");
		    }else{
		      if(m_tap[idx].port[j].DefaultONOFFControl==2){
		        strcpy(str_value,"KEEP");
		      }else if(m_tap[idx].port[j].DefaultONOFFControl==1){
		        strcpy(str_value,"ON");
		      }else{
		        strcpy(str_value,"OFF");
		      }
		    }
		    break;
		  }

		  if((col++)%2==0){
			  fprintf(fp,"<tr id=evenrow align=left>\n");
		  }else{
			  fprintf(fp,"<tr id=oddrow align=left>\n");
		  }
		  fprintf(fp,"<td>%s</td><td>%d</td><td>%s</td><td>%s</td><td>%s</td>\n",m_tap[idx].id,j,str_prop,str_time,str_value);
		  fprintf(fp,"</tr>\n");
		  fflush(fp);
	      }
	    }
	  }
	}
	fprintf(fp,"</table>\n");

	fprintf(fp,"<h2>Translation Map: TAP Status Info. to PointID</h2>\n");
	fprintf(fp,"<table>\n");
	fprintf(fp,"<tr id=headrow><td>TAP_ID</td><td>Port No.</td><td>Propery</td><td>Point ID</td></tr>");
	col=0;
	for(i=0;i<MAXIMUM_SPC1_TAP;i++){
	  if(strlen(ieee1888_pointid_host)>0 && strlen(spc1_tap_id[i])==5){
	    int idx=find_tap_index_by_id(spc1_tap_id[i]);
	    if(idx<0 || n_tap<=idx){
	      continue;
	    }
	    for(j=0;j<4;j++){
	      for(k=0;k<5;k++){
	          char str_prop[30];
		  char pointid[300];
		  switch(k){
		  case 0: strcpy(str_prop,"PE"); break;
		  case 1: strcpy(str_prop,"ONOFFStatus"); break;
		  case 2: strcpy(str_prop,"ONOFFControl"); break;
		  case 3: strcpy(str_prop,"DefaultONOFFStatus"); break;
		  case 4: strcpy(str_prop,"DefaultONOFFControl"); break;
		  }
		  sprintf(pointid,"http://%s/TAP%02d/%d/%s",ieee1888_pointid_host,i,j,str_prop);

	          if(col++%2==0){
	            fprintf(fp,"<tr id=evenrow align=left>\n");
	          }else{
	            fprintf(fp,"<tr id=oddrow align=left>\n");
	          }
	          fprintf(fp,"<td>%s</td><td>%d</td><td>%s</td><td>%s</td>\n",m_tap[idx].id,j,str_prop,pointid);
	          fprintf(fp,"</tr>\n");
	      }
	    } 
	  }
	}
	
	fprintf(fp,"</table>\n");

	fprintf(fp,"<h2>IEEE1888 WRITE Client (Upload) Status</h2>\n");
	fprintf(fp,"<table>\n");
	fprintf(fp,"<tr id=headrow><td>IEEE1888 Server Address (SOAP-EPR)</td><td>Time</td><td>Status</td></tr>");
	for(i=0;i<MAXIMUM_1888SERVER;i++){
		if(strlen(ieee1888server[i])>0){
		  strftime(buf,100,"%Y-%m-%d %H:%M:%S",localtime(&ieee1888_access_time[i]));
		  if(ieee1888_access_time[i]==0){
		    sprintf(buf2,"--");
		  }else if(ieee1888_access_status[i]==0){
		    sprintf(buf2,"OK");
		  }else{
	            sprintf(buf2,"Error(code=%d)",ieee1888_access_status[i]);
		  }
		  if(i%2==0){
			  fprintf(fp,"<tr id=evenrow>\n");
		  }else{
			  fprintf(fp,"<tr id=oddrow>\n");
		  }
		  fprintf(fp,"<td>%s</td><td>%s</td><td>%s</td>\n",ieee1888server[i],buf,buf2);
		  fprintf(fp,"</tr>\n");
		} 
	}
	fprintf(fp,"</table>\n");
	fprintf(fp,"<p></p><p></p>\n");

	time_t t=time(NULL);
	strftime(buf,100,"%Y-%m-%d %H:%M:%S",localtime(&t));
	fprintf(fp,"This page was generated at: %s<br>\n",buf);

	fprintf(fp,"</body>\n");
	fprintf(fp,"</html>\n");

	fclose(fp);
}

pthread_t spc1_status_logging_thread;

void* spc1_logging_thread(void* args){
  while(1){
    put_status_for_notification();
    print_status_in_html();
    sleep(10);
  }
}

// -----------------------------
//    IEEE1888 Client (Upload)
// -----------------------------

pthread_t ieee1888_data_upload_thread[MAXIMUM_1888SERVER];

void* ieee1888_upload_thread(void* args){

  int i,j;
  int no=(int)args;
  time_t last_uploaded=time(NULL);
  while(1){
    time_t now=time(NULL);

    if(strlen(ieee1888server[no])<10){
      sleep(1000);
      continue;
    }

    if((now/10 > last_uploaded/10) || now<last_uploaded){
    
      char buffer[1000];
      ieee1888_transport* request=ieee1888_mk_transport();
      ieee1888_pointSet* tap_ps=ieee1888_mk_pointSet_array(n_tap);
      int n_tap_ps=n_tap;

      request->body=ieee1888_mk_body();
      request->body->pointSet=tap_ps;
      request->body->n_pointSet=n_tap_ps;

      for(i=0;i<n_tap;i++){
        sprintf(buffer,"http://%s/TAP%02d/",ieee1888_pointid_host,i);
        tap_ps[i].id=ieee1888_mk_uri(buffer);

        ieee1888_pointSet* port_ps=ieee1888_mk_pointSet_array(4);
        tap_ps[i].pointSet=port_ps;
        tap_ps[i].n_pointSet=4;

        for(j=0;j<4;j++){
	  sprintf(buffer,"http://%s/TAP%02d/%d/",ieee1888_pointid_host,i,j);
	  port_ps[j].id=ieee1888_mk_uri(buffer);

	  ieee1888_point* type_p=ieee1888_mk_point_array(5);
	  port_ps[j].point=type_p;
	  port_ps[j].n_point=5;

	  sprintf(buffer,"http://%s/TAP%02d/%d/PE",ieee1888_pointid_host,i,j);
	  type_p[0].id=ieee1888_mk_uri(buffer);
	  if(m_tap[i].port[j].PE_time){
	    type_p[0].value=ieee1888_mk_value();
	    type_p[0].n_value=1;
	    type_p[0].value->time=ieee1888_mk_time(m_tap[i].port[j].PE_time);
	    type_p[0].value->content=ieee1888_mk_string(m_tap[i].port[j].PE);
	  }
	  
	  sprintf(buffer,"http://%s/TAP%02d/%d/ONOFFStatus",ieee1888_pointid_host,i,j);
	  type_p[1].id=ieee1888_mk_uri(buffer);
	  if(m_tap[i].port[j].ONOFFStatus_time){
	    type_p[1].value=ieee1888_mk_value();
	    type_p[1].n_value=1;
	    type_p[1].value->time=ieee1888_mk_time(m_tap[i].port[j].ONOFFStatus_time);
            if(m_tap[i].port[j].ONOFFStatus==1){
	      type_p[1].value->content=ieee1888_mk_string("ON");
	    }else{
	      type_p[1].value->content=ieee1888_mk_string("OFF");
	    }
	  }
	  
	  sprintf(buffer,"http://%s/TAP%02d/%d/ONOFFControl",ieee1888_pointid_host,i,j);
	  type_p[2].id=ieee1888_mk_uri(buffer);
	  if(m_tap[i].port[j].ONOFFControl_time){
	    type_p[2].value=ieee1888_mk_value();
	    type_p[2].n_value=1;
	    type_p[2].value->time=ieee1888_mk_time(m_tap[i].port[j].ONOFFControl_time);
            if(m_tap[i].port[j].ONOFFControl==1){
	      type_p[2].value->content=ieee1888_mk_string("ON");
	    }else{
	      type_p[2].value->content=ieee1888_mk_string("OFF");
	    }
	  }

	  sprintf(buffer,"http://%s/TAP%02d/%d/DefaultONOFFStatus",ieee1888_pointid_host,i,j);
	  type_p[3].id=ieee1888_mk_uri(buffer);
	  if(m_tap[i].port[j].DefaultONOFFStatus_time){
	    type_p[3].value=ieee1888_mk_value();
	    type_p[3].n_value=1;
	    type_p[3].value->time=ieee1888_mk_time(m_tap[i].port[j].DefaultONOFFStatus_time);
            if(m_tap[i].port[j].DefaultONOFFStatus==2){
	      type_p[3].value->content=ieee1888_mk_string("KEEP");
            }else if(m_tap[i].port[j].DefaultONOFFStatus==1){
	      type_p[3].value->content=ieee1888_mk_string("ON");
	    }else{
	      type_p[3].value->content=ieee1888_mk_string("OFF");
	    }
	  }
	  
	  sprintf(buffer,"http://%s/TAP%02d/%d/DefaultONOFFControl",ieee1888_pointid_host,i,j);
	  type_p[4].id=ieee1888_mk_uri(buffer);
	  if(m_tap[i].port[j].DefaultONOFFControl_time){
	    type_p[4].value=ieee1888_mk_value();
	    type_p[4].n_value=1;
	    type_p[4].value->time=ieee1888_mk_time(m_tap[i].port[j].DefaultONOFFControl_time);
            if(m_tap[i].port[j].DefaultONOFFControl==2){
	      type_p[4].value->content=ieee1888_mk_string("KEEP");
            }else if(m_tap[i].port[j].DefaultONOFFControl==1){
	      type_p[4].value->content=ieee1888_mk_string("ON");
	    }else{
	      type_p[4].value->content=ieee1888_mk_string("OFF");
	    }
	  }
	}
      }
    
      int err=0;
      ieee1888_dump_objects((ieee1888_object*)request);
      ieee1888_transport* response=ieee1888_client_data(request,ieee1888server[no],NULL,&err);
      ieee1888_dump_objects((ieee1888_object*)response);
        
      ieee1888_access_status[no]=err;
      ieee1888_access_time[no]=time(NULL);
      
      if(request!=NULL){
        ieee1888_destroy_objects((ieee1888_object*)request);
        free(request);
      }
      if(response!=NULL){
        ieee1888_destroy_objects((ieee1888_object*)response);
	free(response);
      }

      printf("thread: trigger %d\n",no);
      last_uploaded=now;
    }
    sleep(2);
  }
}


int xbee_initialize(){
  
  memset(xbee_buffer,0,sizeof(xbee_buffer));

  xbee_fd = open("/dev/ttyAM2", O_RDWR | O_NOCTTY | O_NDELAY);
  if(xbee_fd == -1){
    printf("ERROR: Unable to open /dev/ttyAM2");
    return -1;
  }

  fcntl(xbee_fd, F_SETFL, FNDELAY);  // return asap when read.

  struct termios options;
  tcgetattr(xbee_fd,&options);

  cfsetispeed(&options,B9600);
  cfsetospeed(&options,B9600);

  options.c_cflag |= (CLOCAL | CREAD);
  options.c_lflag ^= (options.c_lflag & ECHO);
  tcsetattr(xbee_fd,TCSANOW, &options);

  return 0;
}

pthread_mutex_t smart_tap_mutex;
pthread_t smart_tap_mgmt_thread;

int find_tap_index_by_id(const char* id){

  int i;
  for(i=0;i<n_tap;i++){
    if(strcmp(m_tap[i].id,id)==0){
      return i;
    }
  }
  return -1;
}

void* smart_tap_thread(void* args){

  while(1){
    /* read characters into our string buffer until we get a CR or NL */
    xbee_nbytes=0;
    xbee_bufptr = xbee_buffer;
    memset(xbee_buffer,0,sizeof(xbee_buffer));
    while ((xbee_nbytes = read(xbee_fd, xbee_bufptr, xbee_buffer + sizeof(xbee_buffer) - xbee_bufptr - 1)) > 0){
      xbee_bufptr += xbee_nbytes;
      if (xbee_bufptr[-1] == '\n' || xbee_bufptr[-1] == '\r'){
        break;
      }
    }

    if(xbee_nbytes>0){
      xbee_buffer[xbee_nbytes-1]='\0';
      printf("%s\n",xbee_buffer);

      char tap_id[6];
      char port0[6];
      char port1[6];
      char port2[6];
      char port3[6];
      if(strstr(xbee_buffer,"SPC1;PE;ID=")==xbee_buffer 
        && xbee_buffer[16]==';'
	&& xbee_buffer[22]==';'
	&& xbee_buffer[28]==';'
	&& xbee_buffer[34]==';'){
        strncpy(tap_id,&xbee_buffer[11],5); tap_id[5]='\0';
        strncpy(port0,&xbee_buffer[17],5); port0[5]='\0';
        strncpy(port1,&xbee_buffer[23],5); port1[5]='\0';
        strncpy(port2,&xbee_buffer[29],5); port2[5]='\0';
        strncpy(port3,&xbee_buffer[35],5); port3[5]='\0';
	
	int idx=find_tap_index_by_id(tap_id);
	time_t t=time(NULL);
	if(0<=idx && idx<n_tap){
	  pthread_mutex_lock(&smart_tap_mutex);
          strcpy(m_tap[idx].port[0].PE,port0);
          strcpy(m_tap[idx].port[1].PE,port1);
          strcpy(m_tap[idx].port[2].PE,port2);
          strcpy(m_tap[idx].port[3].PE,port3);
	  m_tap[idx].port[0].PE_time=t;
	  m_tap[idx].port[1].PE_time=t;
	  m_tap[idx].port[2].PE_time=t;
	  m_tap[idx].port[3].PE_time=t;
	  pthread_mutex_unlock(&smart_tap_mutex);
	}

      }else if(strstr(xbee_buffer,"SPC1;ONOFFStat;ID=")==xbee_buffer
        && xbee_buffer[23]==';'
        && xbee_buffer[25]==';'
        && xbee_buffer[27]==';'
        && xbee_buffer[29]==';'
	){
        strncpy(tap_id,&xbee_buffer[18],5); tap_id[5]='\0';
        strncpy(port0,&xbee_buffer[24],1); port0[1]='\0';
        strncpy(port1,&xbee_buffer[26],1); port1[1]='\0';
        strncpy(port2,&xbee_buffer[28],1); port2[1]='\0';
        strncpy(port3,&xbee_buffer[30],1); port3[1]='\0';
	// printf("parse: ONOFFStatus: %s(%d) %s %s %s %s\n",tap_id,find_tap_index_by_id(tap_id),port0,port1,port2,port3);
	
	int idx=find_tap_index_by_id(tap_id);
	time_t t=time(NULL);
	if(0<=idx && idx<n_tap){
	  pthread_mutex_lock(&smart_tap_mutex);
          m_tap[idx].port[0].ONOFFStatus=atoi(port0);
          m_tap[idx].port[1].ONOFFStatus=atoi(port1);
          m_tap[idx].port[2].ONOFFStatus=atoi(port2);
          m_tap[idx].port[3].ONOFFStatus=atoi(port3);
	  m_tap[idx].port[0].ONOFFStatus_time=t;
	  m_tap[idx].port[1].ONOFFStatus_time=t;
	  m_tap[idx].port[2].ONOFFStatus_time=t;
	  m_tap[idx].port[3].ONOFFStatus_time=t;
	  pthread_mutex_unlock(&smart_tap_mutex);
	}

      }else if(strstr(xbee_buffer,"SPC1;DONOFFStat;ID=")==xbee_buffer
        && xbee_buffer[24]==';'
        && xbee_buffer[26]==';'
        && xbee_buffer[28]==';'
        && xbee_buffer[30]==';'
      ){
        strncpy(tap_id,&xbee_buffer[19],5); tap_id[5]='\0';
        strncpy(port0,&xbee_buffer[25],1); port0[1]='\0';
        strncpy(port1,&xbee_buffer[27],1); port1[1]='\0';
        strncpy(port2,&xbee_buffer[29],1); port2[1]='\0';
        strncpy(port3,&xbee_buffer[31],1); port3[1]='\0';
	
	int idx=find_tap_index_by_id(tap_id);
	time_t t=time(NULL);
	if(0<=idx && idx<n_tap){
	  pthread_mutex_lock(&smart_tap_mutex);
          m_tap[idx].port[0].DefaultONOFFStatus=atoi(port0);
          m_tap[idx].port[1].DefaultONOFFStatus=atoi(port1);
          m_tap[idx].port[2].DefaultONOFFStatus=atoi(port2);
          m_tap[idx].port[3].DefaultONOFFStatus=atoi(port3);
	  m_tap[idx].port[0].DefaultONOFFStatus_time=t;
	  m_tap[idx].port[1].DefaultONOFFStatus_time=t;
	  m_tap[idx].port[2].DefaultONOFFStatus_time=t;
	  m_tap[idx].port[3].DefaultONOFFStatus_time=t;
	  pthread_mutex_unlock(&smart_tap_mutex);
	}
      }
      fflush(stdout);
    }
  }
}

void init(){
  int i;

  load_config("./SPC1_1888GW.conf");

  for(i=0;i<MAXIMUM_SPC1_TAP;i++){
    memset(&m_tap[i],0,sizeof(m_tap[i]));
    if(strlen(spc1_tap_id[i])==5){
      sprintf(m_tap[i].id,spc1_tap_id[i]);
    }else{
      sprintf(m_tap[i].id,"xxxxx");
    }
  }
  n_tap=MAXIMUM_SPC1_TAP;

  xbee_initialize();

  pthread_mutex_init(&smart_tap_mutex,0);
  pthread_create(&smart_tap_mgmt_thread,0,smart_tap_thread,0);

  for(i=0;i<MAXIMUM_1888SERVER;i++){
    pthread_create(&ieee1888_data_upload_thread[i],0,ieee1888_upload_thread,(void*)i);
  }

  pthread_create(&spc1_status_logging_thread,0,spc1_logging_thread,0);
}

#define PARSE_POINT_ID_OK   1
#define PARSE_POINT_ID_FAIL 0

int parse_point_id(const char* point_id, int* tap, int* port, int* type){

  char prefix[100];
  sprintf(prefix,"http://%s/",ieee1888_pointid_host);
  if(strstr(point_id,prefix)==point_id){
    sprintf(prefix,"http://%s/TAP",ieee1888_pointid_host);
    if(strstr(point_id,prefix)==point_id){
      int len=strlen(prefix);
      if(*(point_id+len+2)=='/'){
        char buf[3];
	buf[0]=*(point_id+len);
	buf[1]=*(point_id+len+1);
	buf[2]='\0';
	int tap_no=atoi(buf);
	if(0<=tap_no && tap_no<n_tap){
          // struct smart_tap* tap=&tap[tap_no];
	  *tap=tap_no;

          if(*(point_id+len+2+2)=='/'){
            switch(*(point_id+len+2+1)){
	    case '0': *port=0; break;
	    case '1': *port=1; break;
	    case '2': *port=2; break;
	    case '3': *port=3; break;
	    default: return PARSE_POINT_ID_FAIL;
	    }
            
	    const char* p_type=point_id+len+2+2+1;

	    if(strcmp(p_type,"PE")==0){
              *type=SMART_TAP_PORT_PE;
	    }else if(strcmp(p_type,"ONOFFStatus")==0){
              *type=SMART_TAP_PORT_ONOFFStatus;
	    }else if(strcmp(p_type,"ONOFFControl")==0){
              *type=SMART_TAP_PORT_ONOFFControl;
	    }else if(strcmp(p_type,"DefaultONOFFStatus")==0){
              *type=SMART_TAP_PORT_DefaultONOFFStatus;
	    }else if(strcmp(p_type,"DefaultONOFFControl")==0){
              *type=SMART_TAP_PORT_DefaultONOFFControl;
	    }else{
               return PARSE_POINT_ID_FAIL;
	    }
	    return PARSE_POINT_ID_OK;

	  }else{
            return PARSE_POINT_ID_FAIL;
	  }
	}else{
          return PARSE_POINT_ID_FAIL;
	}
      }else{
        return PARSE_POINT_ID_FAIL;
      }
    }else{
      return PARSE_POINT_ID_FAIL;
    }
  }else{
    return PARSE_POINT_ID_FAIL;
  }
}

void print_ids(){

  int i,p;
  char point_id[100];
  for(i=-1;i<n_tap+2;i++){
    char* id=m_tap[i].id;
    for(p=-1;p<4+1;p++){
      int tap, port, type;

      sprintf(point_id,"http://%s/%s/%d/PE",ieee1888_pointid_host,id,p);
      if(parse_point_id(point_id,&tap,&port,&type)==PARSE_POINT_ID_OK){
        printf("OK -- %s; tap=%d; port=%d; type=%d\n",point_id,tap,port,type);
      }else{
        printf("NG -- %s\n",point_id);
      }
      sprintf(point_id,"http://%s/%s/%d/ONOFFStatus",ieee1888_pointid_host,id,p);
      if(parse_point_id(point_id,&tap,&port,&type)==PARSE_POINT_ID_OK){
        printf("OK -- %s; tap=%d; port=%d; type=%d\n",point_id,tap,port,type);
      }else{
        printf("NG -- %s\n",point_id);
      }
      sprintf(point_id,"http://%s/%s/%d/ONOFFControl",ieee1888_pointid_host,id,p);
      if(parse_point_id(point_id,&tap,&port,&type)==PARSE_POINT_ID_OK){
        printf("OK -- %s; tap=%d; port=%d; type=%d\n",point_id,tap,port,type);
      }else{
        printf("NG -- %s\n",point_id);
      }
      sprintf(point_id,"http://%s/%s/%d/DefaultONOFFStatus",ieee1888_pointid_host,id,p);
      if(parse_point_id(point_id,&tap,&port,&type)==PARSE_POINT_ID_OK){
        printf("OK -- %s; tap=%d; port=%d; type=%d\n",point_id,tap,port,type);
      }else{
        printf("NG -- %s\n",point_id);
      }
      sprintf(point_id,"http://%s/%s/%d/DefaultONOFFControl",ieee1888_pointid_host,id,p);
      if(parse_point_id(point_id,&tap,&port,&type)==PARSE_POINT_ID_OK){
        printf("OK -- %s; tap=%d; port=%d; type=%d\n",point_id,tap,port,type);
      }else{
        printf("NG -- %s\n",point_id);
      }
      sprintf(point_id,"http://%s/%s/%d/PE",ieee1888_pointid_host,id,p);
      if(parse_point_id(point_id,&tap,&port,&type)==PARSE_POINT_ID_OK){
        printf("OK -- %s; tap=%d; port=%d; type=%d\n",point_id,tap,port,type);
      }else{
        printf("NG -- %s\n",point_id);
      }
    }
  }
}

ieee1888_transport* ieee1888_server_query(ieee1888_transport* request, char** args){

  ieee1888_transport* response=(ieee1888_transport*)ieee1888_clone_objects((ieee1888_object*)request,1);
  // TODO: return error if "clone" fails (take compare match)
  
  if(response->body!=NULL){
    ieee1888_destroy_objects((ieee1888_object*)response->body);
    free(response->body);
    response->body=NULL;
  }

  ieee1888_header* header=response->header;
  if(header==NULL){
    response->header=ieee1888_mk_header();
    response->header->error=ieee1888_mk_error_invalid_request("No header in the request.");
    return response;
  }
  if(header->OK!=NULL){
    response->header->error=ieee1888_mk_error_invalid_request("Invalid OK in the header.");
    return response;
  }
  if(header->error!=NULL){
    response->header->error=ieee1888_mk_error_invalid_request("Invalid error in the header.");
    return response;
  }

  ieee1888_query* query=header->query;
  if(header->query==NULL){
    response->header->error=ieee1888_mk_error_invalid_request("No query in the header.");
    return response;
  }

  ieee1888_error* error=NULL;

  if(strcmp(query->type,"storage")==0){
    // TODO: check callbackData
    // TODO: check callbackControl
    // TODO: check 

    // 
    ieee1888_key* keys=query->key;
    int n_keys=query->n_key;

    ieee1888_point* points=NULL;
    int n_points=0;
    if(n_keys>0){
      points=ieee1888_mk_point_array(n_keys);
      n_points=n_keys;
    }

    int i;
    // schema check
    for(i=0;i<n_keys;i++){
      ieee1888_key* key=&keys[i];
      if(key->id==NULL){
        // error -- invalid id
	error=ieee1888_mk_error_invalid_request("ID is missing in the query key");
        break;

      }else if(key->attrName==NULL){
        // error -- invalid attrName
	error=ieee1888_mk_error_invalid_request("attrName is missing in the query key");
        break;

      }else if(strcmp(key->attrName,"time")!=0){
        // error -- unsupported attrName
	error=ieee1888_mk_error_query_not_supported("attrName other than \"time\" are not supported.");
        break;

      }else if(key->eq!=NULL){
        // error -- not supported 
	error=ieee1888_mk_error_query_not_supported("eq in the query key is not supported.");
        break;

      }else if(key->neq!=NULL){
        // error -- not supported 
	error=ieee1888_mk_error_query_not_supported("neq in the query key is not supported.");
        break;

      }else if(key->lt!=NULL){
        // error -- not supported 
	error=ieee1888_mk_error_query_not_supported("lt in the query key is not supported.");
        break;

      }else if(key->gt!=NULL){
        // error -- not supported 
	error=ieee1888_mk_error_query_not_supported("gt in the query key is not supported.");
        break;

      }else if(key->lteq!=NULL){
        // error -- not supported 
	error=ieee1888_mk_error_query_not_supported("lteq in the query key is not supported.");
        break;

      }else if(key->gteq!=NULL){
        // error -- not supported 
	error=ieee1888_mk_error_query_not_supported("gteq in the query key is not supported.");
        break;

      }else if(key->trap!=NULL){
        // error -- not supported 
	error=ieee1888_mk_error_query_not_supported("trap in the query key is not supported.");
        break;

      }else if(key->select!=NULL && strcmp(key->select,"minimum")!=0 && strcmp(key->select,"maximum")!=0){
        // error -- invalid select
	error=ieee1888_mk_error_invalid_request("Invalid select in the query key.");
        break;

      }
    }

    if(error==NULL){

      // check if there is any select="maximum"
      int should_pre_processed=0;
      for(i=0;i<n_keys;i++){
        ieee1888_key* key=&keys[i];
        if(key->select!=NULL && strcmp(key->select,"maximum")==0){
          should_pre_processed=1;
  	  break;
        }
      }

      // pre-processing
      int sending_request=0;
      int onoff_req[n_tap];
      int donoff_req[n_tap];
      memset(onoff_req,0,sizeof(onoff_req));
      memset(donoff_req,0,sizeof(donoff_req));
      for(i=0;i<n_keys;i++){
	int tap_no, port_no, type;
        ieee1888_key* key=&keys[i];
        if(key->select!=NULL && strcmp(key->select,"maximum")==0 
	   && parse_point_id(key->id,&tap_no,&port_no,&type)==PARSE_POINT_ID_OK){
	  if(type==SMART_TAP_PORT_ONOFFStatus){
	    onoff_req[tap_no]=1;
	    sending_request=1;
	  }
	  if(type==SMART_TAP_PORT_DefaultONOFFStatus){
	    donoff_req[tap_no]=1;
	    sending_request=1;
	  }
	}
      }
      if(sending_request){
        char sbuf[40];
        for(i=0;i<n_tap;i++){
	  if(onoff_req[i]){
            sprintf(sbuf,"SPC1;ONOFFStatReq;ID=%s\r\n",m_tap[i].id); // get id from m_tap[i].id
            write(xbee_fd,sbuf,strlen(sbuf));
            printf("send: %s",sbuf); fflush(stdout);
	  }
	  if(donoff_req[i]){
            sprintf(sbuf,"SPC1;DONOFFStatReq;ID=%s\r\n",m_tap[i].id); // get id from m_tap[i].id
            write(xbee_fd,sbuf,strlen(sbuf));
            printf("send: %s",sbuf); fflush(stdout);
	  }
	}
	sleep(2);
      }

      // main data acquisition
      for(i=0;i<n_keys;i++){
	int tap_no, port_no, type;
        ieee1888_key* key=&keys[i];
	if(parse_point_id(key->id,&tap_no,&port_no,&type)==PARSE_POINT_ID_OK){
	  
	  time_t t=0;
	  char value[6];

	  points[i].id=ieee1888_mk_uri(key->id);

	  switch(type){
	  case SMART_TAP_PORT_PE:
	    strncpy(value,m_tap[tap_no].port[port_no].PE,6);
	    t=m_tap[tap_no].port[port_no].PE_time;
	    break;

	  case SMART_TAP_PORT_ONOFFStatus:
	    if(m_tap[tap_no].port[port_no].ONOFFStatus==TAP_ON){
              strcpy(value,"ON");
	    }else{
              strcpy(value,"OFF");
	    }
	    t=m_tap[tap_no].port[port_no].ONOFFStatus_time;
	    break;

	  case SMART_TAP_PORT_ONOFFControl:
	    if(m_tap[tap_no].port[port_no].ONOFFControl==TAP_ON){
              strcpy(value,"ON");
	    }else{
              strcpy(value,"OFF");
	    }
	    t=m_tap[tap_no].port[port_no].ONOFFControl_time;
	    break;

	  case SMART_TAP_PORT_DefaultONOFFStatus:
	    if(m_tap[tap_no].port[port_no].DefaultONOFFStatus==TAP_KEEP){
	      strcpy(value,"KEEP");
	    }else if(m_tap[tap_no].port[port_no].DefaultONOFFStatus==TAP_ON){
              strcpy(value,"ON");
	    }else{
              strcpy(value,"OFF");
	    }
	    t=m_tap[tap_no].port[port_no].DefaultONOFFStatus_time;
	    break;

	  case SMART_TAP_PORT_DefaultONOFFControl:
	    if(m_tap[tap_no].port[port_no].DefaultONOFFControl==TAP_KEEP){
	      strcpy(value,"KEEP");
	    }else if(m_tap[tap_no].port[port_no].DefaultONOFFControl==TAP_ON){
              strcpy(value,"ON");
	    }else{
              strcpy(value,"OFF");
	    }
	    t=m_tap[tap_no].port[port_no].DefaultONOFFControl_time;
	    break;
	  }

	  if(t!=0){
	    ieee1888_value* v=ieee1888_mk_value();
	    v->time=ieee1888_mk_time(t);
	    v->content=ieee1888_mk_string(value);

	    points[i].value=v;
	    points[i].n_value=1;
	  }

	}else{
	  // error -- POINT_NOT_FOUND
	  error=ieee1888_mk_error_point_not_found(key->id);
          break;
	}
      }
    }

    if(error==NULL){
      // if no error
      response->header->OK=ieee1888_mk_OK();
      response->body=ieee1888_mk_body();
      response->body->point=points;
      response->body->n_point=n_points;
    }else{
      response->header->error=error;
      ieee1888_body* body=ieee1888_mk_body();
      body->point=points;
      body->n_point=n_points;
      ieee1888_destroy_objects((ieee1888_object*)body);
      free(body);
    }

  }else if(strcmp(query->type,"stream")==0){
    // not supported
    error=ieee1888_mk_error_query_not_supported("type=\"stream\" in the query is not supported.");
    response->header->error=error;

  }else{
    // error (invalid request)
    error=ieee1888_mk_error_invalid_request("Invalid query type.");
    response->header->error=error;
  }
  return response;
}


ieee1888_error* ieee1888_server_data_parse_request(ieee1888_pointSet* pointSet, int n_pointSet, ieee1888_point* point, int n_point, struct smart_tap* tap){

  int i;
  for(i=0;i<n_pointSet;i++){
    ieee1888_error* error=ieee1888_server_data_parse_request(pointSet[i].pointSet, pointSet[i].n_pointSet, pointSet[i].point, pointSet[i].n_point,tap);
    if(error!=NULL){
      return error;
    }
  }

  for(i=0;i<n_point;i++){

    int tap_no, port_no, type;
    if(parse_point_id(point[i].id,&tap_no,&port_no,&type)==PARSE_POINT_ID_OK){

      switch(type){
      case SMART_TAP_PORT_PE:
        // error (if value is specified) -- forbidden
	if(point[i].n_value>0){
          return ieee1888_mk_error_forbidden("data (WRITE) is not allowed for PE");
	}
        break; 

      case SMART_TAP_PORT_ONOFFStatus:
        // error (if value is specified) -- forbidden
	if(point[i].n_value>0){
          return ieee1888_mk_error_forbidden("data (WRITE) is not allowed for ONOFFStatus");
	}
        break;

      case SMART_TAP_PORT_ONOFFControl:
        // call a handler (shall not commit now)
	if(point[i].n_value>0){
	  if(strcmp(point[i].value[point[i].n_value-1].content,"ON")==0){
            tap[tap_no].port[port_no].ONOFFControl=TAP_ON;
	  }else{
            tap[tap_no].port[port_no].ONOFFControl=TAP_OFF;
	  }
	  tap[tap_no].port[port_no].ONOFFControl_time=-1;
	}
        break;

      case SMART_TAP_PORT_DefaultONOFFStatus:
        // error (if value is specified) -- forbidden
	if(point[i].n_value>0){
          return ieee1888_mk_error_forbidden("data (WRITE) is not allowed for DefaultONOFFStatus");
	}
        break;

      case SMART_TAP_PORT_DefaultONOFFControl:
        // call a handler (shall not commit now)
	if(point[i].n_value>0){
	  if(strcmp(point[i].value[point[i].n_value-1].content,"KEEP")==0){
            tap[tap_no].port[port_no].DefaultONOFFControl=TAP_KEEP;
	  }else if(strcmp(point[i].value[point[i].n_value-1].content,"ON")==0){
            tap[tap_no].port[port_no].DefaultONOFFControl=TAP_ON;
	  }else{
            tap[tap_no].port[port_no].DefaultONOFFControl=TAP_OFF;
	  }
	  tap[tap_no].port[port_no].DefaultONOFFControl_time=-1;
        }
        break;
      }
    }
  }
  return NULL;
}

ieee1888_error* ieee1888_server_query_commit_ondemand_request(struct smart_tap* tap){
  return NULL;
}

ieee1888_error* ieee1888_server_data_commit_request(struct smart_tap* tap){

  int i;
  int p;
  for(i=0;i<n_tap;i++){
    int xbee_send;
    char sbuf[50];

    // for ONOFFControl case
    xbee_send=0;
    sprintf(sbuf,"SPC1;ONOFFCtrl;ID=%s;x;x;x;x\r\n",m_tap[i].id); // get id from m_tap[i].id
    for(p=0;p<4;p++){
      if(tap[i].port[p].ONOFFControl_time){
         sbuf[24+p*2]=(char)(tap[i].port[p].ONOFFControl)+'0';
         xbee_send=1;
      }
    }
    if(xbee_send){
      write(xbee_fd,sbuf,strlen(sbuf));
      usleep(150000);
      // printf("send: %s",sbuf); fflush(stdout);
    }

    // for DONOFFControl case
    xbee_send=0;
    sprintf(sbuf,"SPC1;DONOFFCtrl;ID=%s;x;x;x;x\r\n",m_tap[i].id); // get id from m_tap[i].id 
    for(p=0;p<4;p++){
      if(tap[i].port[p].DefaultONOFFControl_time){
         // send via xbee
         sbuf[25+p*2]=(char)(tap[i].port[p].DefaultONOFFControl)+'0';
         xbee_send=1;
      }
    }
    if(xbee_send){
      write(xbee_fd,sbuf,strlen(sbuf));
      usleep(150000);
      // printf("send: %s",sbuf); fflush(stdout);
    }

    for(p=0;p<4;p++){  
      pthread_mutex_lock(&smart_tap_mutex);
      if(tap[i].port[p].ONOFFControl_time){
        m_tap[i].port[p].ONOFFControl=tap[i].port[p].ONOFFControl;
	m_tap[i].port[p].ONOFFControl_time=time(NULL);
      }
      if(tap[i].port[p].DefaultONOFFControl_time){
        m_tap[i].port[p].DefaultONOFFControl=tap[i].port[p].DefaultONOFFControl;
	m_tap[i].port[p].DefaultONOFFControl_time=time(NULL);
      }
      pthread_mutex_unlock(&smart_tap_mutex);
    }
  }
  return NULL;
}

ieee1888_transport* ieee1888_server_data(ieee1888_transport* request,char** args){

  ieee1888_transport* response=ieee1888_mk_transport();

  // TODO: check the validity of body 
  ieee1888_body* body=request->body;

  struct smart_tap* tap=(struct smart_tap*)calloc(sizeof(struct smart_tap),n_tap);

  ieee1888_error* error=ieee1888_server_data_parse_request(body->pointSet,body->n_pointSet,body->point,body->n_point,tap);
  if(error!=NULL){
     response->header=ieee1888_mk_header();
     response->header->error=error;

     free(tap);
     return response;
  }

  error=ieee1888_server_data_commit_request(tap);
  if(error!=NULL){
     response->header=ieee1888_mk_header();
     response->header->error=error;

     free(tap);
     return response;
  }

  response->header=ieee1888_mk_header();
  response->header->OK=ieee1888_mk_OK();

  free(tap);
  return response;
}



int main(int argc,char** argv){
  
  init();
  
  ieee1888_set_service_handlers(ieee1888_server_query,ieee1888_server_data);
  ieee1888_server_create(atoi(ieee1888_hosting_port));

  return 1;
}


