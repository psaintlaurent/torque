#include "license_pbs.h" /* See here for the software license */
#include <stdlib.h>
#include <stdio.h> /* fprintf */

#include "list_link.h" /* list_link */
#include "attribute.h" /* pbs_attribute, attribute_def */
#include <string>


ssize_t read_nonblocking_socket(int fd, void *buf, ssize_t count)
  {
  fprintf(stderr, "The call to read_nonblocking_socket needs to be mocked!!\n");
  exit(1);
  }

int write_buffer(char *buf, int len, int fds)
  {
  fprintf(stderr, "The call to write_buffer needs to be mocked!!\n");
  exit(1);
  }

void delete_link(struct list_link *old)
  {
  fprintf(stderr, "The call to delete_link needs to be mocked!!\n");
  exit(1);
  }

ssize_t write_nonblocking_socket(int fd, const void *buf, ssize_t count)
  {
  fprintf(stderr, "The call to write_nonblocking_socket needs to be mocked!!\n");
  exit(1);
  }

int find_attr(struct attribute_def *attr_def, const char *name, int limit)
  {
  fprintf(stderr, "The call to find_attr needs to be mocked!!\n");
  exit(1);
  }

void *get_next(list_link pl, char *file, int line)
  {
  fprintf(stderr, "The call to get_next needs to be mocked!!\n");
  exit(1);
  }

int attr_to_str(std::string&, attribute_def *at_def, struct pbs_attribute attr, int XML)
  {
  fprintf(stderr, "The call to attr_to_str needs to be mocked!!\n");
  exit(1);
  }

/*int ctnodes(

  char *spec)

  {
  return(1);
  }*/

#if 0

void clear_dynamic_string(dynamic_string *ds)
  {
  fprintf(stderr, "The call to attr_to_str needs to be mocked!!\n");
  exit(1);
  }

dynamic_string *get_dynamic_string(int initial_size, const char *str)
  {
  fprintf(stderr, "The call to attr_to_str needs to be mocked!!\n");
  exit(1);
  }

void free_dynamic_string(dynamic_string *ds)
  {
  fprintf(stderr, "The call to attr_to_str needs to be mocked!!\n");
  exit(1);
  }
#endif



extern char saveBuff[];
extern int saveBuffRdPtr;
extern int saveBuffEndPtr;

ssize_t read_ac_socket(int fd, void *buf, ssize_t count)
  {
  int lenRead = 0;
  int i;
  char *buff = (char *)buf;
  for(i=0;(i<count)&&(saveBuffRdPtr < saveBuffEndPtr);i++)
    {
    *buff++ = saveBuff[saveBuffRdPtr++];
    lenRead++;
    }
  return (lenRead);
  }

void log_err(int errnum, const char *routine, const char *text) {}
void log_record(int eventtype, int objclass, const char *objname, const char *text) {}
void log_event(int eventtype, int objclass, const char *objname, const char *text) {}
