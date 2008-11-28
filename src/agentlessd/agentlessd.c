/* @(#) $Id$ */

/* Copyright (C) 2008 Third Brigade, Inc.
 * All rights reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 3) as published by the FSF - Free Software
 * Foundation
 */



#include "shared.h"
#include "agentlessd.h"


/* send integrity checking message. */
int send_intcheck_msg(char *host, char *msg)
{
    char sys_location[1024 +1];

    sys_location[1024] = '\0';
    snprintf(sys_location, 1024, "%s->%s", host, SYSCHECK);
    
    if(SendMSG(lessdc.queue, msg, sys_location, SYSCHECK_MQ) < 0)
    {
        merror(QUEUE_SEND, ARGV0);

        if((lessdc.queue = StartMQ(DEFAULTQPATH,WRITE)) < 0)
        {
            ErrorExit(QUEUE_FATAL, ARGV0, DEFAULTQPATH);
        }

        /* If we reach here, we can try to send it again */
        SendMSG(lessdc.queue, msg, sys_location, SYSCHECK_MQ);
    }

    return(0);
}



/* Renames the diff file after completed. */
int rename_diff_file(char *host, char *script)
{
    char sys_location[1024 +1];
    char new_location[1024 +1];
          
    sys_location[1024] = '\0';
    new_location[1024] = '\0';

    snprintf(sys_location, 1024, "%s/%s->%s/%s", DIFF_DIR_PATH, host, script,
             DIFF_NEW_TMP);
    snprintf(new_location, 1024, "%s/%s->%s/%s", DIFF_DIR_PATH, host, script,
             DIFF_NEW_FINAL);

    if(rename(sys_location, new_location) != 0)
    {
        merror(RENAME_ERROR, ARGV0, sys_location);
    }

    return(0);
}



/* get the diff file. */
FILE *open_diff_file(char *host, char *script)
{
    FILE *fp = NULL;
    char sys_location[1024 +1];
          
    sys_location[1024] = '\0';
    snprintf(sys_location, 1024, "%s/%s->%s/%s", DIFF_DIR_PATH, host, script,
             DIFF_NEW_TMP);


    fp = fopen(sys_location, "w");

    /* If we can't open, try creating the directory. */
    if(!fp)
    {
        snprintf(sys_location, 1024, "%s/%s->%s", DIFF_DIR_PATH, host, script);
        if(IsDir(sys_location) == -1)
        {
            if(mkdir(sys_location, 0770) == -1)
            {
                merror(MKDIR_ERROR, ARGV0, sys_location);
                return(NULL);
            }
        }

        snprintf(sys_location, 1024, "%s/%s->%s/%s", DIFF_DIR_PATH, host, 
                 script, DIFF_NEW_TMP);
        fp = fopen(sys_location, "w");
        if(!fp)
        {
            merror(FOPEN_ERROR, ARGV0, sys_location);
            return(NULL);
        }
    }

    return(fp);
}



/* Run periodic commands. */
int run_periodic_cmd(agentlessd_entries *entry, int test_it)
{
    int i = 0;
    char *tmp_str;
    char buf[OS_SIZE_2048 +1];
    char command[OS_SIZE_1024 +1];
    FILE *fp;
    FILE *fp_store = NULL;
    
    
    buf[0] = '\0';
    command[0] = '\0';
    command[OS_SIZE_1024] = '\0'; 
    
    
    while(entry->server[i])
    {
        /* Ignored entry. */
        if(entry->server[i][0] == '\0')
        {
            i++;
            continue;
        }
        
        
        /* We only test for the first server entry. */ 
        else if(test_it)
        {
            int ret_code = 0;
            snprintf(command, OS_SIZE_1024, "%s/%s test test >/dev/null 2>&1", 
                    AGENTLESSDIRPATH, entry->type);
            ret_code = system(command);

            /* Checking if the test worked. */
            if(ret_code != 0)
            {
                if(ret_code == 32512)
                {
                    merror("%s: ERROR: Expect command not found (or bad "
                           "arguments) for '%s'.",
                           ARGV0, entry->type); 
                }
                merror("%s: ERROR: Test failed for '%s' (%d). Ignoring.",
                       ARGV0, entry->type, ret_code/256);
                entry->error_flag = 99;
                return(-1);
            }

            verbose("%s: INFO: Test passed for '%s'.", ARGV0, entry->type);
            return(0);
        }
        
        snprintf(command, OS_SIZE_1024, "%s/%s \"%s\" %s 2>&1", 
                AGENTLESSDIRPATH, entry->type, entry->server[i], 
                entry->options);


        fp = popen(command, "r");
        if(fp)
        {
            while(fgets(buf, OS_SIZE_2048, fp) != NULL)
            {
                /* Removing new lines or carriage returns. */
                tmp_str = strchr(buf, '\r');
                if(tmp_str)
                    *tmp_str = '\0';
                tmp_str = strchr(buf, '\n');
                if(tmp_str)
                    *tmp_str = '\0';
                    
                if(strncmp(buf, "ERROR: ", 7) == 0)
                {
                    merror("%s: ERROR: %s: %s: %s", ARGV0, 
                           entry->type, entry->server[i], buf +7);
                    entry->error_flag++;
                    break;
                }
                else if(strncmp(buf, "INFO: ", 6) == 0)
                {
                    verbose("%s: INFO: %s: %s: %s", ARGV0, 
                            entry->type, entry->server[i], buf +6);
                }
                else if(strncmp(buf, "FWD: ", 4) == 0)
                {
                    tmp_str = buf + 5;
                    send_intcheck_msg(entry->server[i], tmp_str);
                }
                else if((entry->state & LESSD_STATE_DIFF) &&
                        (strncmp(buf, "STORE: ", 7) == 0))
                {
                    fp_store = open_diff_file(entry->server[i], entry->type);
                }
                else if(fp_store)
                {
                    fprintf(fp_store, "%s\n", buf);
                }
                else
                {
                    debug1("%s: DEBUG: buffer: %s", ARGV0, buf);
                }
            }

            if(fp_store)
            {
                fclose(fp_store);
                fp_store = NULL;

                rename_diff_file(entry->server[i], entry->type);
            }
            pclose(fp);
        }
        else
        {
            merror("%s: ERROR: popen failed on '%s' for '%s'.", ARGV0, 
                   entry->type, entry->server[i]);
            entry->error_flag++;
        }

        i++;
    }

    if(fp_store)
    {
        fclose(fp_store);
    }
    
    return(0);
}



/* Main agentlessd */
void Agentlessd()
{
    time_t tm;     
    struct tm *p;       

    int today = 0;		        
    int thismonth = 0;
    int thisyear = 0;
    int test_it = 1;

    char str[OS_SIZE_1024 +1];


    /* Waiting a few seconds to settle */
    sleep(2);
    memset(str, '\0', OS_SIZE_1024 +1);
    
    
    /* Getting currently time before starting */
    tm = time(NULL);
    p = localtime(&tm);	
    
    today = p->tm_mday;
    thismonth = p->tm_mon;
    thisyear = p->tm_year+1900;
                

    /* Connecting to the message queue
     * Exit if it fails.
     */
    if((lessdc.queue = StartMQ(DEFAULTQPATH, WRITE)) < 0)
    {
        ErrorExit(QUEUE_FATAL, ARGV0, DEFAULTQUEUE);
    }



    /* Main monitor loop */
    while(1)
    {
        int i = 0;
        tm = time(NULL);
        p = localtime(&tm);


        /* Day changed, deal with log files */
        if(today != p->tm_mday)
        {
            today = p->tm_mday;
            thismonth = p->tm_mon;
            thisyear = p->tm_year+1900;
        }


        while(lessdc.entries[i])
        {
            if(lessdc.entries[i]->error_flag >= 10)
            {
                if(lessdc.entries[i]->error_flag != 99)
                {
                    merror("%s: ERROR: Too many failures for '%s'. Ignoring it.",
                           ARGV0, lessdc.entries[i]->type); 
                    lessdc.entries[i]->error_flag = 99;
                }

                i++;
                sleep(i);
                continue;
            }

            
            /* Run the check again if the frequency has elapsed. */
            if((lessdc.entries[i]->state & LESSD_STATE_PERIODIC) &&
               ((lessdc.entries[i]->current_state + 
                 lessdc.entries[i]->frequency) < tm))
            {
                run_periodic_cmd(lessdc.entries[i], test_it);
                if(!test_it)
                    lessdc.entries[i]->current_state = tm;
            }
            
            i++;

            sleep(i);
        }
        
        /* We only check every minute */
        test_it = 0;
        sleep(60);
    }
}

/* EOF */