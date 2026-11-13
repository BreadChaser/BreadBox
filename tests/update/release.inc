/* Same production translation unit without BB_UPDATE_DEV_URL, real local TLS. */
static int release_transport_test(int argc,char **argv)
{
 assert(argc==3);
 char connect[128];snprintf(connect,sizeof(connect),":443:127.0.0.1:%s",argv[1]);
 discovery_connect=curl_slist_append(NULL,connect);assert(discovery_connect);
 snprintf(connect,sizeof(connect),"github.com:%s:127.0.0.1:%s",argv[2],argv[2]);
 discovery_connect=curl_slist_append(discovery_connect,connect);assert(discovery_connect);
 mkdir("sdmc:",0700);mkdir("sdmc:/3ds",0700);mkdir(APP_SD_ROOT,0700);mkdir(UPDATE_ROOT,0700);
 const char *good[]={"https://github.com/asset","https://objects.githubusercontent.com/asset?token=x","https://release-assets.githubusercontent.com/asset?token=x"};
 for(unsigned i=0;i<3;i++)assert(update_transport_url(good[i]));
 const char *bad[]={NULL,"http://github.com/asset","https://github.com.evil/asset","https://github.com@foreign.test/asset","https://foreign.test/asset","https://github.com:443/asset","https://github.com\\@foreign.test/asset","https://sub.github.com/asset"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(!update_transport_url(bad[i]));
 /* Discovery always starts at the fixed URL. Server advances through trusted
  * CDN, foreign HTTPS, downgrade, redirect loop, bad hostname and bad CA. */
 update_discover_job(NULL);
 assert(!strcmp(transport_request,RELEASE_API) && update_status==2 && update_machine.state==BB_UPDATE_DOWNLOAD);
 for(unsigned i=0;i<5;i++){
  update_machine.state=BB_UPDATE_IDLE;update_discover_job(NULL);
  assert(!strcmp(transport_request,RELEASE_API) && update_status!=2 && update_machine.state==BB_UPDATE_IDLE && !starts);
 }
 /* A trusted redirect chain is reusable for ranged CIA chunks. Foreign finals
  * fail even with valid TLS and HTTP 206, and must never enter the CDN cache. */
 char bytes[128];const char *source="https://github.com/BreadChaser/BreadBox/releases/download/v1/BreadBox.cia";
 for(unsigned i=0;i<4;i++){
  long code=0;sink s={.file=tmpfile(),.limit=sizeof(bytes)};assert(s.file);
  bool ok=update_fetch(source,&s,"0-31",&code);fclose(s.file);
  if(i==0){assert(ok && code==206 && s.used==32);assert(!strcmp(update_asset_url,"https://release-assets.githubusercontent.com/asset"));}
  if(i==1){assert(ok && code==206 && s.used==32);assert(!strcmp(transport_request,"https://release-assets.githubusercontent.com/asset"));update_asset_url[0]=0;}
  if(i==2){assert(!ok && code==206 && s.used==32 && !update_asset_url[0]);}
  if(i==3){assert(!ok && !update_asset_url[0]);}
 }
 /* Reject a foreign cached CDN URL before configuring or sending a request. */
 strcpy(update_asset_source,source);strcpy(update_asset_url,"https://foreign.test/asset");
 sink cached={.file=tmpfile(),.limit=sizeof(bytes)};long code=0;assert(cached.file);
 assert(!update_fetch(source,&cached,"0-31",&code) && !cached.used && !update_asset_url[0] && !transport_options);fclose(cached.file);
 curl_slist_free_all(discovery_connect);discovery_connect=NULL;update_network_close();
 puts("PASS: release HTTPS/TLS/CA/discovery options, trusted CDN redirects/cache/Range, foreign final/cached hosts, HTTP downgrade, redirect limit, hostname and CA failures");
 return 0;
}
