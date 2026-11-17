/* Restart uses a fresh process: only files carry consent and the verified bytes. */
static void robust_finish(void)
{
 assert(!bb_update_tick() && update_thread);
 for(unsigned turn=0;bb_update_installing();turn++){assert(turn<10000);bb_update_install_frame();usleep(1000);}
 assert(bb_update_committed() && starts==1 && finishes==1);
 bb_update_shutdown();bb_writers_release_exclusive();update_commit=0;
}
static int robust_boot(void)
{
 update_prefs_loaded=0;update_initialized=true;update_net_busy=update_net_abort=0;
 update_initialize_job(NULL);
 assert(update_preference==2 && update_approved && update_explicit_check);
 assert(update_machine.state==BB_UPDATE_READY && update_checked && !update_postponed);
 assert(!update_thread && !starts && !queued);
 game=true;assert(!bb_update_tick() && !starts && !queued);game=false;
 robust_finish();assert(!queued);
 return 0;
}
static void robust_restart(char **argv)
{
 pid_t pid=fork();assert(pid>=0);
 if(!pid){assert(!setenv("BB_ROBUST_BOOT","1",1));execl(argv[0],argv[0],argv[1],(char *)NULL);_exit(127);}
 int status;assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && !WEXITSTATUS(status));
}
static void robust_pending(void)
{
 stage();update_pending_saved=false;assert(update_pending_store());
 FILE *f=fopen(PREF,"w");assert(f);assert(fputs("2\n",f)>=0);assert(!fclose(f));
 safe_remove(RECEIPT);safe_remove(STATUS);safe_remove(BACKOFF);
}
static int robust_test(char **argv)
{
 /* Each binary variant starts with a separate update history. */
 safe_remove(ACCEPTED);safe_remove(RECEIPT);safe_remove(STATUS);safe_remove(BACKOFF);
 /* Settings accepts and stages, then exits directly; restart needs no manifest GET. */
 arm_install();update_preference=2;update_approved=false;update_explicit_check=update_explicit_pending=true;
 update_machine.state=BB_UPDATE_DOWNLOAD;update_view_refresh();update_postponed=true;update_countdown_since=100;
 assert(update_explicit_result() && update_approved && !update_postponed && !update_countdown_since);
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_VERIFY);
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_READY && !starts);
 update_view_refresh();assert(!strcmp(update_settings_status(),"Ready. Reopen to install"));
 FILE *f=fopen(PREF,"w");assert(f);fputs("2\n",f);fclose(f);
 robust_restart(argv);
 /* Explicit acceptance also overrides an actual Automatic countdown postponement. */
 safe_remove(ACCEPTED);arm_install();update_approved=update_explicit_check=false;
 update_idle_since=fake_time;update_countdown_since=0;held=0;
 fake_time+=5000;assert(!bb_update_tick() && update_countdown_since && !update_thread);
 held=KEY_A;assert(!bb_update_tick() && update_postponed);held=0;
 update_machine.state=BB_UPDATE_DOWNLOAD;update_explicit_pending=update_explicit_check=true;
 update_view_refresh();assert(update_explicit_result() && !update_postponed);
 update_stage_job(NULL);update_stage_job(NULL);robust_finish();
 /* Leaving Updates keeps the same accepted job advancing on other menus. */
 safe_remove(ACCEPTED);arm_install();update_explicit_check=true;update_preference=2;
 update_machine.state=BB_UPDATE_DOWNLOAD;update_net_busy=update_net_abort=0;accept_queue=true;
 for(unsigned turn=0;update_machine.state!=BB_UPDATE_READY;turn++){
  assert(turn<4);unsigned count=queued;bb_update_menu_tick();assert(queued==count+1 && queued_fn==update_stage_job);
  queued_fn(NULL);assert(!starts && !update_thread);
 }
 unsigned count=queued;bb_update_menu_tick();assert(queued==count && !starts);
 game=true;update_machine.state=BB_UPDATE_DOWNLOAD;bb_update_menu_tick();assert(queued==count);game=false;
 update_machine.state=BB_UPDATE_READY;accept_queue=false;robust_finish();
 /* Room instructions persist while blocked, then power/space recovery starts by itself. */
 for(unsigned blocker=0;blocker<2;blocker++){
  arm_install();update_explicit_check=true;update_notice_until=0;
  adapter=blocker!=0;percent=9;battery=0;free_clusters=blocker?0:100000;
  assert(!bb_update_tick() && !starts && !update_thread);
  notice_seen[0]=0;bb_update_draw_notice();
  assert(!strcmp(notice_seen,blocker?"Free 3 MB on SD card":"Battery low. Plug in"));
  adapter=true;percent=100;battery=5;free_clusters=100000;fake_time+=5001;
  robust_finish();notice_seen[0]=0;update_commit=1;bb_update_draw_notice();assert(!notice_seen[0]);update_commit=0;
 }
 /* A condition can worsen after the room check but before the AM preflight. */
 for(unsigned blocker=0;blocker<2;blocker++){
  arm_install();update_explicit_check=true;update_notice_until=0;
  update_power_checked=fake_time;update_power_cached=update_space_cached=true;
  adapter=blocker!=0;percent=9;battery=0;free_clusters=blocker?0:100000;
  assert(!bb_update_tick() && update_thread);
  for(unsigned turn=0;bb_update_installing();turn++){assert(turn<10000);bb_update_install_frame();usleep(1000);}
  assert(!starts && !finishes && !cancels && update_machine.state==BB_UPDATE_READY && !update_install_attempted);
  assert(!bb_update_tick() && !update_thread);
  notice_seen[0]=0;bb_update_draw_notice();
  assert(!strcmp(notice_seen,blocker?"Free 3 MB on SD card":"Battery low. Plug in"));
  for(unsigned turn=0;turn<20;turn++)assert(!bb_update_tick() && !update_thread && !starts);
  adapter=true;percent=100;battery=5;free_clusters=100000;fake_time+=5001;
  assert(!bb_update_tick() && update_thread);
  notice_seen[0]=0;bb_update_draw_notice();assert(!notice_seen[0]);robust_finish();
 }
 /* Percentage boundary, every fallback level, independent telemetry failures. */
 adapter=false;
 for(percent=0;percent<=100;percent++)assert(update_power_ready()==(percent>=10));
 mcu_ok=false;
 for(battery=0;battery<=5;battery++)assert(update_power_ready()==(battery>0));
 battery=0;adapter_result=-1;assert(update_power_ready());adapter_result=0;
 battery_result=-1;assert(update_power_ready());battery_result=0;
 telemetry_ok=false;assert(update_power_ready());telemetry_ok=true;
 mcu_ok=true;percent_result=-1;assert(!update_power_ready());percent_result=0;
 percent=10;assert(update_power_ready());assert(!strcmp(power_line,"update: power charger=0 battery=10 rc=00000000 ready=1\n"));
 percent=9;assert(!update_power_ready());assert(!strcmp(power_line,"update: power charger=0 battery=9 rc=00000000 ready=0\n"));
 adapter=true;percent=0;assert(update_power_ready());percent=100;battery=5;
 /* Restart after power, HOME, sleep, or GPU cancellation uses the same accepted bytes. */
 for(unsigned reason=0;reason<4;reason++){
  robust_pending();update_initialized=true;update_approved=update_explicit_check=update_checked=true;
  update_check_now=false;update_machine.state=BB_UPDATE_READY;update_install_attempted=false;update_power_checked=0;
  if(!reason){adapter=false;percent=9;battery=0;assert(!bb_update_tick() && !starts);adapter=true;percent=100;battery=5;}
  else{
   gpu_busy=reason==3;sleep_advances=reason==3;
   assert(!bb_update_tick() && update_thread);
   for(unsigned turn=0;!update_finish_ready;turn++){assert(turn<10000);usleep(1000);}
   if(reason<3)update_apt(reason==1?APTHOOK_ONSUSPEND:APTHOOK_ONSLEEP,NULL);
   else for(unsigned turn=0;bb_update_installing();turn++){assert(turn<100000);bb_update_install_frame();usleep(200);}
   sleep_advances=gpu_busy=false;bb_update_tick();
   assert(!bb_update_committed() && !update_thread && cancels==1 && !finishes);
   update_notice_until=0;notice_seen[0]=0;bb_update_draw_notice();assert(!strcmp(notice_seen,"Reopen to retry update"));
   unsigned count=starts;for(unsigned i=0;i<20;i++)assert(!bb_update_tick() && starts==count);
  }
  robust_restart(argv);
 }
 /* A second-check failure needs a new launch, so say the required action. */
 safe_remove(ACCEPTED);safe_remove(BACKOFF);update_failed_count=0;update_pending_saved=false;arm_install();update_explicit_check=true;
 f=fopen(STAGE,"r+b");assert(f);fputc('X',f);fclose(f);
 assert(!bb_update_tick() && update_thread);
 for(unsigned turn=0;bb_update_installing();turn++){assert(turn<10000);bb_update_install_frame();usleep(1000);}
 assert(!bb_update_tick() && !update_thread && !starts && update_status==5 && access(STAGE,F_OK));
 notice_seen[0]=0;bb_update_draw_notice();assert(!strcmp(notice_seen,"Reopen to retry update"));
 assert(!access(ACCEPTED,F_OK));
 /* Damaged stage is discarded and fetched again; two bad hashes impose a bound. */
 robust_pending();f=fopen(STAGE,"r+b");assert(f);fputc('X',f);fclose(f);
 update_net_busy=update_net_abort=0;update_failed_count=0;update_initialize_job(NULL);
 assert(update_machine.state==BB_UPDATE_DOWNLOAD && access(STAGE,F_OK) && update_failed_count==1);
 char connect[128];const char *port=strrchr(argv[1],':');assert(port);
 snprintf(connect,sizeof(connect),"127.0.0.1:80:127.0.0.1:%s",port+1);
 discovery_connect=curl_slist_append(NULL,connect);assert(discovery_connect);
 update_stage_job(NULL);update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_READY && update_verify(NULL));
 f=fopen(STAGE,"r+b");assert(f);fputc('X',f);fclose(f);update_machine.state=BB_UPDATE_VERIFY;
 update_stage_job(NULL);assert(update_machine.state==BB_UPDATE_FAILED && update_status==8 && update_failed_count==2);
 update_initialize_job(NULL);assert(update_machine.state==BB_UPDATE_FAILED && update_status==8 && access(STAGE,F_OK));
 /* Expiration queues one LOW retry even if this launch remains open. */
 update_machine.state=BB_UPDATE_IDLE;update_check_now=false;update_retry_after=0;accept_queue=true;
 fake_time+=86400000;count=queued;game=true;bb_update_menu_tick();assert(queued==count);game=false;
 assert(update_poll(true)==UPDATE_POLL_WAIT && queued==count+1 && queued_fn==update_pending_retry_job);
 assert(update_poll(true)==UPDATE_POLL_WAIT && queued==count+1);
 queued_fn(NULL);assert(update_machine.state==BB_UPDATE_DOWNLOAD && !update_job_busy);accept_queue=false;
 safe_remove(ACCEPTED);safe_remove(BACKOFF);update_failed_count=0;update_approved=false;update_failure_reason[0]=0;
 /* Space is an actionable storage failure, including during download. */
 free_clusters=0;update_machine.state=BB_UPDATE_IDLE;update_discover_job(NULL);
 assert(update_status==17 && update_machine.state==BB_UPDATE_IDLE);
 update_view_refresh();assert(!strcmp(update_settings_status(),"Free 5 MB on SD card"));
 stage();assert(!update_install(NULL) && update_status==17 && !starts);
 update_view.state=BB_UPDATE_IDLE;assert(!strcmp(update_settings_status(),"Free 3 MB on SD card"));
 remove(STAGE);assert(!update_download(NULL) && update_status==17);
 free_clusters=100000;space_result=-1;assert(!update_require_space(2097152) && update_status==9);space_result=0;
 /* A complete cached download only needs install space, with no discovery margin. */
 robust_pending();free_clusters=600;update_approved=false;update_install_attempted=false;update_power_checked=0;update_initialize_job(NULL);
 assert(update_machine.state==BB_UPDATE_READY);robust_finish();free_clusters=100000;
 /* Every unsupported launch medium/identity gets the installed-app message before networking. */
 safe_remove(ACCEPTED);update_machine.state=BB_UPDATE_IDLE;update_view.state=BB_UPDATE_IDLE;starts=0;
 for(unsigned launch=0;launch<4;launch++){
  homebrew=launch==0;self=BB_UPDATE_TITLE+(launch==1);launch_media=launch==2?0:MEDIATYPE_SD;media_result=launch==3?-1:0;
  update_initialize_job(NULL);assert(!strcmp(update_settings_status(),"Needs the installed app"));
  unsigned count=queued;update_check_now=true;assert(update_poll(false)==UPDATE_POLL_WAIT && queued==count && !starts);
 }
 homebrew=false;self=BB_UPDATE_TITLE;launch_media=MEDIATYPE_SD;media_result=0;assert(update_title_version());
 update_approved=update_explicit_check=update_explicit_pending=update_install_attempted=false;update_failure_reason[0]=0;
 const char *expected[]={"No connection. Check Wi-Fi","Bad update info. Try later","Bad download. Will retry","Install failed. Reopen app"};
 for(unsigned status=3;status<=6;status++){update_status=status;assert(!strcmp(update_settings_status(),expected[status-3]));}
 for(unsigned status=8;status<=18;status++){update_status=status;assert(update_settings_status()[0] && (status!=17 || strstr(update_settings_status()," MB ")));}
 curl_slist_free_all(discovery_connect);discovery_connect=NULL;update_network_close();
 puts("PASS: accepted Settings restart, postponed explicit consent, PTM telemetry, bounded persistent install retry, damaged refetch, space reasons, SD launch identity");
 return 0;
}
