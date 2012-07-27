#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<errno.h>
#include<unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include"../include/types.h"
#include"../include/eeprom.h" //SFPS_MAX, SFP_PN_LEN, EE_BASE_SFP, EE_BASE_INIT

#define START_SFPSEC "##SFP_SECTION"
#define END_SFPSEC "##END_SFP_SECTION"
#define START_INITSEC "##INIT_SECTION"
#define END_INITSEC "##END_INIT_SECTION"
#define TLV_FILE "wrc.tlv"
#define SECTION_DONE 0
#define SECTION_START 1
#define SECTION_FLUSH 2
#define TLV_HDR_LEN 5


int line_empty(char *line, unsigned int len);
int create_tlv(struct s_sfpinfo *sfps, int sfplen, char *initbuf, int buflen, uint8_t is_end);
int32_t swap_endian(int32_t a);
int sfp_extract(char *line, unsigned int len, struct s_sfpinfo *sfp);

int main(int argc, char **argv)
{
  FILE *file;
  char *buf=NULL;
  struct s_sfpinfo sfps[SFPS_MAX];
  uint8_t p_sfp=0, p_init=0;
  int idx=0, size, buflen;

  if( argc<2 )
  {
    fprintf(stderr, "Input file not given\n");
    return -1;
  }

  file=fopen(argv[1], "r");
  if(!file)
  {
    fprintf(stderr, "Could not open the input file\n");
    return -1;
  }

  while(!feof(file))
  {
    if(buf!=NULL) free(buf);
    buf = NULL;
    buflen = getline(&buf, &size, file);

    if( line_empty(buf, buflen) ) continue;
    if(!strncmp(buf, START_SFPSEC, buflen-2))       { p_sfp=SECTION_START; continue; }
    else if(!strncmp(buf, END_SFPSEC, buflen-2))    { p_sfp=SECTION_FLUSH; }
    else if(!strncmp(buf, START_INITSEC, buflen-2)) { p_init=SECTION_START; continue; }
    else if(!strncmp(buf, END_INITSEC, buflen-2))   { p_init=SECTION_FLUSH; }

    if(p_sfp==SECTION_START && p_init==SECTION_START)
    {
      fprintf(stderr, "Syntax error in input file!!!\n");
      fclose(file);
      free(buf);
      return -1;
    }

    if(p_sfp==SECTION_START)
    {
      buf[buflen-1] = '\0'; //drop \n at the end of the line
      sfp_extract(buf, strlen(buf), &sfps[idx++]);
      if(idx>=SFPS_MAX) 
      {
        fprintf(stderr, "Too many SFPS in the file (over SFPS_MAX)\n");
        fclose(file);
        free(buf);
        return -1;
      }
    }
    else if(p_sfp==SECTION_FLUSH)
    {
      create_tlv(sfps, idx, NULL, 0, 0);
      p_sfp=SECTION_DONE;
    }
    else if(p_init==SECTION_START)
    {
      if(create_tlv(NULL, 0, buf, buflen, 0) < 0)
      {
        fclose(file);
        free(buf);
        return -1;
      }
    }
    else if(p_init==SECTION_FLUSH)
    {
      create_tlv(NULL, 0, NULL, 0, 1);
      p_init=SECTION_DONE;
    }

  }
  fclose(file);
  free(buf);

  printf("TLV file created\n");
  return 0;
}

int sfp_extract(char *line, unsigned int len, struct s_sfpinfo *sfp)
{
  int i,j=2;
  char *ptr[3];
  uint8_t *chkptr;

  for(i=len-1; i>=0; --i)
  {
    if( line[i]==' ' )
    {
      ptr[j--] = line+i+1;
      line[i]='\0';
    }
    if(j<0) break; //we have all parameters
  }
  strcpy(sfp->pn, line);
  sfp->pn[strlen(line)]=' ';
  for(i=strlen(line); i<SFP_PN_LEN; i++)
    sfp->pn[i]=' ';
  sfp->dTx = atoi(ptr[0]);
  sfp->dRx = atoi(ptr[1]);
  sfp->alpha = atoi(ptr[2]);

  /*count checksum*/
  sfp->chksum = 0;
  chkptr = (uint8_t*)sfp;
  for(i=0; i<sizeof(struct s_sfpinfo)-1; ++i) //'-1' because we do not include chksum in computation
    sfp->chksum = (uint8_t) ((uint16_t)sfp->chksum + *(chkptr++)) & 0xff;

#if 0
  printf("TEST: pn=");
  for(i=0;i<SFP_PN_LEN; ++i)
    printf("%c", sfp->pn[i]);
  printf(", dTx=%u, dRx=%u, alpha=%d(0x%x)\n", sfp->dTx, sfp->dRx, sfp->alpha, sfp->alpha);
#endif

  return 0;
}

int line_empty(char *line, unsigned int len)
{
  int i;

  if(line==NULL || len==-1)
    return 1;

  for(i=0; i<len; ++i)
  {
    if(line[i]!=' ' || line[i]!='\n' || line[i]!='\0')
      return 0;
  }

  return 1;
}


int create_tlv(struct s_sfpinfo *sfps, int sfplen, char *initbuf, int buflen, uint8_t is_end)
{
  static int fd;
  int i;
  uint32_t tmpbuf;
  static uint32_t init_tlv_base=0;
  static uint8_t f_open=1, f_init=1;
  static uint16_t init_sz=0;

  if(f_open)
  {
    fd = open(TLV_FILE, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if(fd==-1)
    {
      perror("Could not write to sfp file");
      return -1;
    }
    f_open = 0;
  }

  if(sfplen>0)
  {
    if(!f_init) //the init script was writen earlier
    {
      lseek(fd, init_tlv_base, SEEK_SET);
      tmpbuf = 'w';
      write(fd, &tmpbuf, 1);
      tmpbuf = EE_BASE_INIT;
      write(fd, &tmpbuf, 2);
      tmpbuf = init_sz + 2; //+2 for _bytes used_
      write(fd, &tmpbuf, 2);
      write(fd, &init_sz, 2);
    }
    //tlv write operation
    tmpbuf = 'w';
    write(fd, &tmpbuf, 1);
    //tlv base address
    tmpbuf = EE_BASE_SFP;
    write(fd, &tmpbuf, 2);
    //tlv section size
    tmpbuf = sfplen*sizeof(struct s_sfpinfo) + 1; //+1 for the count(1B) -> see dev/eeprom.c
    init_tlv_base = tmpbuf + TLV_HDR_LEN; 
    write(fd, &tmpbuf, 2);
    //count(1B)
    write(fd, &sfplen, 1);
    for(i=0; i<sfplen; ++i)
    {
      sfps[i].dTx = swap_endian(sfps[i].dTx);
      sfps[i].dRx = swap_endian(sfps[i].dRx);
      sfps[i].alpha = swap_endian(sfps[i].alpha);
      write(fd, (uint8_t*)&sfps[i], sizeof(struct s_sfpinfo) );
    }  
  } 
  else if(buflen>0)
  {
    if(f_init) lseek(fd, init_tlv_base+7, SEEK_SET);
    f_init = 0;

    if(buflen)
    {
      printf("init cmd\n");
      write(fd, initbuf, buflen);
      init_sz += buflen;
    }
  }

  if(is_end && !f_init) 
  {
    lseek(fd, init_tlv_base, SEEK_SET);
    tmpbuf = 'w';
    write(fd, &tmpbuf, 1);
    tmpbuf = EE_BASE_INIT;
    write(fd, &tmpbuf, 2);
    tmpbuf = init_sz + 2; //+2 for _bytes used_
    write(fd, &tmpbuf, 2);
    tmpbuf = (init_sz&0xff00)>>8 | (init_sz&0x00ff)<<8; //swap endianness
    write(fd, &tmpbuf, 2);
  }
  if(is_end) close(fd);

  return 0;
}

int32_t swap_endian(int32_t a)
{
  return (a&0xff)<<24 | (a&0xff00)<<8 | (a&0xff0000)>>8 | (a&0xff000000)>>24;
}
