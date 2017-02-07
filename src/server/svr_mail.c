/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * svr_mail.c - send mail to mail list or owner of job on
 * job begin, job end, and/or job abort
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "pbs_ifl.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "pbs_job.h"
#include "log.h"
#include "../lib/Liblog/pbs_log.h"
#include "../lib/Liblog/log_event.h"
#include "server.h"
#include "utils.h"
#include "threadpool.h"
#include "svrfunc.h" /* get_svr_attr_* */
#include "work_task.h"
#include "mail_throttler.hpp"

/* Unit tests should use the special unit test sendmail command */
#ifdef UT_SENDMAIL_CMD
#undef SENDMAIL_CMD
#define SENDMAIL_CMD UT_SENDMAIL_CMD
#endif

/* External Functions Called */

extern void svr_format_job (FILE *, mail_info *, const char *);
extern int  listening_socket;

/* Global Data */
extern struct server server;

extern int LOGLEVEL;

mail_throttler pending_emails;


void add_body_info(

  char      *bodyfmtbuf /* I */,
  mail_info *mi /* I */)

  {
  char *bodyfmt = NULL;
  bodyfmt =  strcpy(bodyfmtbuf, "PBS Job Id: %i\n"
                                  "Job Name:   %j\n");
  if (mi->exec_host.size() != 0)
    {
    strcat(bodyfmt, "Exec host:  %h\n");
    }

  strcat(bodyfmt, "%m\n");

  if (mi->text.size() != 0)
    {
    strcat(bodyfmt, "%d\n");
    }

  if (mi->errFile.size() != 0)
    {
    strcat(bodyfmt, "Error_Path: %k\n");
    }

  if (mi->outFile.size() != 0)
    {
    strcat(bodyfmt, "Output_Path: %l\n");
    }
  }


/*
 * write_email()
 *
 * In emailing, the mail body is written to a pipe connected to
 * standard input for sendmail. This function supplies the body
 * of the message.
 *
 */
void write_email(

  FILE      *outmail_input,
  mail_info *mi)

  {
  char       *bodyfmt = NULL;
  const char *subjectfmt = NULL;
  char bodyfmtbuf[MAXLINE];

  /* Pipe in mail headers: To: and Subject: */
  fprintf(outmail_input, "To: %s\n", mi->mailto.c_str());

  /* mail subject line formating statement */
  get_svr_attr_str(SRV_ATR_MailSubjectFmt, (char **)&subjectfmt);
  if (subjectfmt == NULL)
    {
    subjectfmt = "PBS JOB %i";
    }

  fprintf(outmail_input, "Subject: ");
  svr_format_job(outmail_input, mi, subjectfmt);
  fprintf(outmail_input, "\n");

  /* Set "Precedence: bulk" to avoid vacation messages, etc */
  fprintf(outmail_input, "Precedence: bulk\n\n");

  /* mail body formating statement */
  get_svr_attr_str(SRV_ATR_MailBodyFmt, &bodyfmt);
  if (bodyfmt == NULL)
    {
    add_body_info(bodyfmtbuf, mi);
    bodyfmt = bodyfmtbuf;
    }

  /* Now pipe in the email body */
  svr_format_job(outmail_input, mi, bodyfmt);

  } /* write_email() */



/*
 * get_sendmail_args()
 *
 * Populates the arguments to sendmail
 */

int get_sendmail_args(

  char       *sendmail_args[],
  mail_info  *mi,
  char      **mailptr_ptr)

  {
  int         rc = PBSE_NONE;
  int         numargs = 0;
  const char *mailfrom = NULL;
  char       *mailptr;
  
  /* Who is mail from, if SRV_ATR_mailfrom not set use default */
  get_svr_attr_str(SRV_ATR_mailfrom, (char **)&mailfrom);
  if (mailfrom == NULL)
    {
    mailfrom = PBS_DEFAULT_MAIL;
    if (LOGLEVEL >= 5)
      {
      char tmpBuf[LOG_BUF_SIZE];

      snprintf(tmpBuf,sizeof(tmpBuf),
        "Updated mailfrom to default: '%s'\n",
        mailfrom);
      log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        mi->jobid.c_str(),
        tmpBuf);
      }
    }

  sendmail_args[numargs++] = (char *)SENDMAIL_CMD;
  sendmail_args[numargs++] = (char *)"-f";
  sendmail_args[numargs++] = (char *)mailfrom;

  /* Add the e-mail addresses to the command line */
  *mailptr_ptr = strdup(mi->mailto.c_str());
  mailptr = *mailptr_ptr;
  sendmail_args[numargs++] = mailptr;
  for (int counter = 0; counter < (int)strlen(mailptr); counter++)
    {
    if (mailptr[counter] == ',')
      {
      mailptr[counter] = '\0';
      sendmail_args[numargs++] = mailptr + counter + 1;
      if (numargs >= 99)
        break;
      }
    }

  sendmail_args[numargs] = NULL;

  return(rc);
  } // END get_sendmail_args()



/*
 * fork_and_exec_child()
 *
 * Forks and execs the sendmail child
 *
 */

int fork_and_exec_child(

  FILE        **stream_ptr,
  std::string  &error_msg,
  char         *sendmail_args[],
  pid_t        &pid)

  {
  int          pipes[2];
  /* Create a pipe to talk to the sendmail process we are about to fork */
  if (pipe(pipes) == -1)
    {
    error_msg = "Unable to pipes for sending e-mail";
    return(-1);
    }

  if ((pid=fork()) == -1)
    {
    error_msg = "Unable to fork for sending e-mail";

    close(pipes[0]);
    close(pipes[1]);
    return(-1);
    }
  else if (pid == 0)
    {
    /* CHILD */

    /* Make stdin the read end of the pipe */
    dup2(pipes[0],STDIN_FILENO);

    /* Close the rest of the open file descriptors */
    int numfds = sysconf(_SC_OPEN_MAX);
    while (--numfds > 0)
      close(numfds);

    execv(SENDMAIL_CMD, sendmail_args);

    // If execv returns (only on error) make sure the child goes away
    exit(1);
    }
  else
    {
    // We are the parent
    // Save the write end of the pipe for later
    *stream_ptr = fdopen(pipes[1], "w");

    // Close the read end of the pipe
    close(pipes[0]);
    }

  return(PBSE_NONE);
  } // END fork_and_exec_child()



/*
 * cleanup_from_sending_email()
 *
 */

void cleanup_from_sending_email(

  FILE       *stream,
  char       *mailptr,
  const char *jobid,
  pid_t       pid)

  {
  int  status = 0;
  char tmpBuf[LOCAL_LOG_BUF_SIZE];

  fflush(stream);

  /* Close and wait for the command to finish */
  if (fclose(stream) != 0)
    {
    snprintf(tmpBuf,sizeof(tmpBuf),
      "Piping mail body to sendmail closed: errno %d:%s\n",
      errno, strerror(errno));

    log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      jobid,
      tmpBuf);
    }

  // we aren't going to block in order to find out whether or not sendmail worked 
  if ((waitpid(pid, &status, WNOHANG) != 0) &&
      (status != 0))
    {
    snprintf(tmpBuf,sizeof(tmpBuf),
      "Sendmail command returned %d. Mail may not have been sent\n",
      status);

    log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      jobid,
      tmpBuf);
    }

  // don't leave zombies
  while (waitpid(-1, &status, WNOHANG) != 0)
    {
    // zombie reaped, NO-OP
    }
    
  free(mailptr);
  } // END cleanup_from_sending_email()



/*
 * send_the_mail()
 *
 * In emailing, we fork and exec sendmail providing the body of
 * the message on standard in.
 *
 */
void *send_the_mail(

  void *vp)

  {
  mail_info   *mi = (mail_info *)vp;

  pid_t        pid;
  std::string  error_msg;
  // We call sendmail with cmd_name + 2 arguments + # of mailto addresses + 1 for null
  char        *sendmail_args[100];
  FILE        *stream;
  char        *mailptr;

  get_sendmail_args(sendmail_args, mi, &mailptr);

  if (fork_and_exec_child(&stream, error_msg, sendmail_args, pid) != PBSE_NONE)
    {
    log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      mi->jobid.c_str(),
      error_msg.c_str());
    
    delete mi;
    free(mailptr);
    return(NULL);
    }

  // We are the parent at this point, the child exits inside fork_and_exec_child()

  /* Write the body to the pipe */
  write_email(stream, mi);

  cleanup_from_sending_email(stream, mailptr, mi->jobid.c_str(), pid);

  delete mi;

  return(NULL);
  } /* END send_the_mail() */



void set_output_files(

  svr_job   *pjob,
  mail_info *mi)

  {
  const char *join_val = pjob->get_str_attr(JOB_ATR_join);
  if (join_val != NULL)
    {
    if (!strcmp(join_val, "oe"))
      {
      mi->errFile = pjob->get_str_attr(JOB_ATR_outpath);
      mi->outFile = pjob->get_str_attr(JOB_ATR_outpath);
      }
    else if (!strcmp(join_val, "eo"))
      {
      mi->errFile = pjob->get_str_attr(JOB_ATR_errpath);
      mi->outFile = pjob->get_str_attr(JOB_ATR_errpath);
      }
    }

  if (mi->outFile.size() == 0)
    mi->outFile = pjob->get_str_attr(JOB_ATR_outpath);

  if (mi->errFile.size() == 0)
    mi->errFile = pjob->get_str_attr(JOB_ATR_errpath);
  } // END set_output_files()
             


void send_email_batch(
    
  struct work_task *pwt)

  {
  std::vector<mail_info>  pending_list;
  char                   *addressee = (char *)pwt->wt_parm1;
  pid_t                   pid;
  std::string             error_msg;
  // We call sendmail with cmd_name + 2 arguments + # of mailto addresses + 1 for null
  char                   *sendmail_args[100];
  FILE                   *stream;
  char                   *mailptr;

  free(pwt->wt_mutex);
  free(pwt);

  if ((pending_emails.get_email_list(addressee, pending_list) == PBSE_NONE) &&
      (pending_list.size() > 0))
    {
    unsigned int size = pending_list.size();

    get_sendmail_args(sendmail_args, &pending_list[0], &mailptr);

    if (fork_and_exec_child(&stream, error_msg, sendmail_args, pid) != PBSE_NONE)
      {
      log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pending_list[0].jobid.c_str(),
        error_msg.c_str());
      
      free(mailptr);
      return;
      }

    if (size == 1)
      {
      write_email(stream, &pending_list[0]);
      }
    else
      {
      char *bodyfmt = NULL;
      char  subject_fmt[MAXLINE];
      char  bodyfmtbuf[MAXLINE];
        
      get_svr_attr_str(SRV_ATR_MailBodyFmt, &bodyfmt);

      /* Pipe in mail headers: To: and Subject: */
      fprintf(stream, "To: %s\n", pending_list[0].mailto.c_str());

      /* mail subject line formating statement */
      snprintf(subject_fmt, sizeof(subject_fmt), "Summary Email for %u Torque Jobs", size);
      fprintf(stream, "Subject: %s\n", subject_fmt);

      /* Set "Precedence: bulk" to avoid vacation messages, etc */
      fprintf(stream, "Precedence: bulk\n\n");

      /* Now pipe in the email body */
      for (unsigned int i = 0; i < size; i++)
        {
        /* mail body formating statement */
        if (bodyfmt == NULL)
          {
          add_body_info(bodyfmtbuf, &pending_list[i]);
          bodyfmt = bodyfmtbuf;
          }

        fprintf(stream, "Job '%s'\n", pending_list[i].jobid.c_str());
        svr_format_job(stream, &pending_list[i], bodyfmt);
        fprintf(stream, "\n");
        }
      }
  
    cleanup_from_sending_email(stream, mailptr, "summary of several jobs", pid);
    }

  free(addressee);
  } // END send_email_batch()



void svr_mailowner_with_message(

  svr_job    *pjob,      /* I */
  int         mailpoint, /* note, single character  */
  int         force,     /* if set to MAIL_FORCE, force mail delivery */
  const char *text, /* text to mail. */
  const char *msg)   /* Optional extra message */

  {
  if((text == NULL)||(*text == '\0'))
    {
    return;
    }
  if((msg == NULL)||(*msg == '\0'))
    {
    return svr_mailowner(pjob,mailpoint,force,text);
    }

  std::string newMsg(text);
  newMsg += "\n";
  newMsg += msg;

  svr_mailowner(pjob, mailpoint, force, newMsg.c_str());
  }



void svr_mailowner(

  svr_job   *pjob,      /* I */
  int    mailpoint, /* note, single character  */
  int    force,     /* if set to MAIL_FORCE, force mail delivery */
  const char  *text)      /* (optional) additional message text */

  {
  char                  mailto[1024];
  char                 *domain = NULL;
  int                   i;
  mail_info             mi;
  bool                  no_force = false;

  struct array_strings *pas;
  memset(mailto, 0, sizeof(mailto));

  get_svr_attr_str(SRV_ATR_MailDomain, &domain);
  if ((domain != NULL) &&
      (!strcasecmp("never", domain)))
    {		
    /* never send user mail under any conditions */
    if (LOGLEVEL >= 3) 
      {
      log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->get_jobid(),
        "Not sending email: Mail domain set to 'never'\n");
      }

    return;
    }
    
  //check to see if the user does not want any mail sent, not even on job failures
  const char *mailpnts = pjob->get_str_attr(JOB_ATR_mailpnts);
	if ((mailpnts != NULL) && 
	    (*(mailpnts) ==  MAIL_NOJOBMAIL))
    {
    if (LOGLEVEL >= 3)
      {
      log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->get_jobid(),
        "Not sending email: mail option disabled by user for this job \n");			
      }
    
    return;        
    }

  /*
   * if force is true, force the mail out regardless of mailpoint
   * unless server no_mail_force attribute is set to true
   */
  get_svr_attr_b(SRV_ATR_NoMailForce, &no_force);

  if ((force != MAIL_FORCE) ||
      (no_force == true))
    {

    if (mailpnts != NULL)
      {
      if ((strchr(mailpnts, MAIL_NONZERO) != NULL) &&
          (pjob->get_exec_exitstat() == JOB_EXEC_OK))
        {
        log_event(PBSEVENT_JOB,
                  PBS_EVENTCLASS_JOB,
                  pjob->get_jobid(),
                  "Not sending email: User does not want mail of a zero exit code for a job.\n");
        return;
        }
      else if ((strchr(mailpnts, MAIL_NONZERO) != NULL) &&
               (pjob->get_exec_exitstat() != JOB_EXEC_OK))
        {
        if (LOGLEVEL >= 3)
          {
          log_event(PBSEVENT_JOB,
                    PBS_EVENTCLASS_JOB,
                    pjob->get_jobid(),
                    "sending email: job requested e-mail on all non-zero exit codes");
          }
        }
      else if (*(mailpnts) ==  MAIL_NONE)
        {
        /* do not send mail. No mail requested on job */
        log_event(PBSEVENT_JOB,
                  PBS_EVENTCLASS_JOB,
                  pjob->get_jobid(),
                  "Not sending email: job requested no e-mail");
        return;
        }
      /* see if user specified mail of this type */
      else if (strchr(mailpnts, mailpoint) == NULL)
        {
        /* do not send mail */
        log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->get_jobid(),
          "Not sending email: User does not want mail of this type.\n");

        return;
        }
      }
    else if (mailpoint != MAIL_ABORT) /* not set, default to abort */
      {
      log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->get_jobid(),
        "Not sending email: Default mailpoint does not include this type.\n");

      return;
      }
    }

  if (LOGLEVEL >= 3)
    {
    char tmpBuf[LOG_BUF_SIZE];

    snprintf(tmpBuf, LOG_BUF_SIZE, "preparing to send '%c' mail for job %s to %s (%.64s)\n",
             (char)mailpoint,
             pjob->get_jobid(),
             pjob->get_str_attr(JOB_ATR_job_owner),
             (text != NULL) ? text : "---");

    log_event(
      PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
      PBS_EVENTCLASS_JOB,
      pjob->get_jobid(),
      tmpBuf);
    }

  /* Who does the mail go to?  If mail-list, them; else owner */
  mailto[0] = '\0';

  if (pjob->is_attr_set(JOB_ATR_mailuser))
    {
    /* has mail user list, send to them rather than owner */

    pas = pjob->get_arst_attr(JOB_ATR_mailuser);

    if (pas != NULL)
      {
      for (i = 0;i < pas->as_usedptr;i++)
        {
        if ((strlen(mailto) + strlen(pas->as_string[i]) + 2) < sizeof(mailto))
          {
          if (mailto[0] != '\0')
            strcat(mailto, ",");

          strcat(mailto, pas->as_string[i]);
          }
        }
      }
      mailto[strlen(mailto)] = '\0';
    }
  else
    {
    /* no mail user list, just send to owner */

    if (domain != NULL)
      {
      snprintf(mailto, sizeof(mailto), "%s@%s",
        pjob->get_str_attr(JOB_ATR_euser), domain);

      if (LOGLEVEL >= 5) 
        {
        char tmpBuf[LOG_BUF_SIZE];

        snprintf(tmpBuf,sizeof(tmpBuf),
          "Updated mailto from job owner and mail domain: '%s'\n",
          mailto);
        log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->get_jobid(),
          tmpBuf);
        }
      }
    else
      {
#ifdef TMAILDOMAIN
      snprintf(mailto, sizeof(mailto), "%s@%s",
        pjob->get_str_attr(JOB_ATR_euser), TMAILDOMAIN);
#else /* TMAILDOMAIN */
      snprintf(mailto, sizeof(mailto), "%s", pjob->get_str_attr(JOB_ATR_job_owner));
#endif /* TMAILDOMAIN */

      if (LOGLEVEL >= 5)
        {
        char tmpBuf[LOG_BUF_SIZE];

        snprintf(tmpBuf,sizeof(tmpBuf),
          "Updated mailto from job owner: '%s'\n",
          mailto);
        log_event(PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->get_jobid(),
          tmpBuf);
        }
      }
    }

  /* initialize the mail information */
  mi.mailto = mailto;
  mi.mail_point = mailpoint;

  if (pjob->get_str_attr(JOB_ATR_exec_host) != NULL)
    mi.exec_host = pjob->get_str_attr(JOB_ATR_exec_host);

  mi.jobid = pjob->get_jobid();

  if (pjob->get_str_attr(JOB_ATR_jobname) != NULL)
    mi.jobname = pjob->get_str_attr(JOB_ATR_jobname);

  if (mailpoint == (int) MAIL_END)
    set_output_files(pjob, &mi);

  if (text)
    {
    mi.text = text;
    }

  long email_delay = 0;
  get_svr_attr_l(SRV_ATR_EmailBatchSeconds, &email_delay);

  if (email_delay == 0)
    {
    /* have a thread do the work of sending the mail */
    enqueue_threadpool_request(send_the_mail, new mail_info(mi), task_pool);
    }
  else if (pending_emails.add_email_entry(mi) == true)
    {
    // This is the first for this addressee so set a task
    set_task(WORK_Timed,
             time(NULL) + email_delay,
             send_email_batch,
             strdup(mi.mailto.c_str()),
             FALSE);
    }

  return;
  }  /* END svr_mailowner() */

/* END svr_mail.c */
