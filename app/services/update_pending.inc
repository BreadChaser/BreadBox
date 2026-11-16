/* Accepted manifests are local retry records, not a different release format.
 * Read them through the same parser and recheck the bytes before every install. */
static bool PSX3D_COLD update_pending_store(void)
{
 if(update_pending_saved)return true;
 FILE *f=safe_begin(ACCEPTED);if(!f)return false;
 bool wrote=fprintf(f,"{\"title_id\":\"000400000B0B0B00\",\"title_version\":\"%u\",\"version\":\"%s\",\"size\":\"%lu\",\"sha256\":\"%s\",\"url\":\"%s\"}\n",
  update_manifest.version,update_manifest.display,(unsigned long)update_manifest.size,update_manifest.sha256,update_manifest.url)>0;
 update_pending_saved=safe_finish(f,ACCEPTED,wrote);
 return update_pending_saved;
}
static void PSX3D_COLD update_pending_resume(void)
{
 safe_recover(ACCEPTED);FILE *f=fopen(ACCEPTED,"rb");if(!f)return;
 char data[2049];size_t n=fread(data,1,sizeof(data),f);bool io=ferror(f)!=0;if(fclose(f))io=true;
 if(io){update_status=9;return;}
 if(!bb_update_manifest_parse(data,n,&update_manifest)){
  safe_remove(ACCEPTED);remove(STAGE);update_status=4;return;
 }
 if(!bb_update_newer(BB_UPDATE_TITLE,update_manifest.version,update_installed_version)){
  safe_remove(ACCEPTED);remove(STAGE);remove(STAGE ".sha");update_status=7;return;
 }
 update_pending_saved=update_approved=update_explicit_check=update_checked=true;
 update_postponed=false;update_countdown_since=0;update_check_now=false;
 if(update_hash_backoff()){update_status=8;update_machine.state=BB_UPDATE_FAILED;return;}
 struct stat st;bool complete=!stat(STAGE,&st) && st.st_size==update_manifest.size;
 update_machine.state=BB_UPDATE_DOWNLOAD;update_status=2;
 if(complete && update_verify(NULL))update_machine.state=BB_UPDATE_READY;
 else if(update_hash_backoff()){update_machine.state=BB_UPDATE_FAILED;update_status=8;}
 log_printf("update: accepted version=%u resumed phase=%u download=%d\n",update_manifest.version,(unsigned)update_machine.state,update_machine.state==BB_UPDATE_DOWNLOAD);
}
/* The cooldown can expire without another launch; all file work stays on LOW. */
static void PSX3D_COLD update_pending_retry_job(void *unused)
{
 (void)unused;
 if(update_title_version())update_pending_resume();
 __atomic_store_n(&update_job_busy,0,__ATOMIC_RELEASE);
}
