/* Native host test includes the actual transport to exercise private parsers. */
#define GITHUB_SEED_PATH "/tmp/openhome-https-test.seed"
#define GITHUB_UPDATE_TEST 1
#include "source/github_update.c"
#include <assert.h>
char net_error[96];
int main(int argc,char **argv){
    assert(argc==4);
    char host[96],path[2048];
    assert(!parse_url(MANIFEST_URL,host,path));
    assert(!parse_url(argv[1],host,path));
    assert(parse_url("http://github.com/hugoalves6/OpenHome-LAN/releases/download/x/x",host,path));
    assert(parse_url("https://github.com/hugoalves6/OpenHome-LAN/releases/downloadEVIL/x",host,path));
    assert(parse_url("https://github.com/evil/repo/releases/download/x/x",host,path));
    assert(parse_url("https://github.com.evil.com/a",host,path));
    assert(parse_url("https://github.com:443/hugoalves6/OpenHome-LAN/releases/download/x/x",host,path));
    assert(parse_url("https://raw.githubusercontent.com/evil/repo/a",host,path));
    assert(github_update_newer("0.5.0","0.4.5"));
    assert(github_update_newer("0.5.10","0.5.9"));
    assert(!github_update_newer("0.5.0","0.5.0"));
    assert(!github_update_newer("0.4.5","0.5.0"));
    assert(!github_update_newer("0.6.0-rc1","0.5.0"));
    remove(GITHUB_SEED_PATH);assert(!github_update_ready());
    github_update_seed("00112233445566778899aabbccddeeff");assert(github_update_ready());
    int result=github_update_download(argv[1],"/tmp/openhome-github-download",argv[2]);
    if(result)fprintf(stderr,"HTTPS failure: %s; TLS=%d transport=%d\n",net_error,br_ssl_engine_last_error(&client.eng),transport_failed);
    assert(!result);
    assert(github_update_download(argv[1],"/tmp/openhome-github-bad","0000000000000000000000000000000000000000000000000000000000000000"));
    assert(access("/tmp/openhome-github-bad",F_OK));
    test_reject_certificates=1;
    assert(github_update_download(argv[1],"/tmp/openhome-github-untrusted",argv[2]));
    assert(br_ssl_engine_last_error(&client.eng)==BR_ERR_X509_NOT_TRUSTED);
    assert(access("/tmp/openhome-github-untrusted",F_OK));
    test_reject_certificates=0;test_server_name="not-github.invalid";
    assert(github_update_download(argv[1],"/tmp/openhome-github-wrong-name",argv[2]));
    assert(br_ssl_engine_last_error(&client.eng)==BR_ERR_X509_BAD_SERVER_NAME);
    assert(access("/tmp/openhome-github-wrong-name",F_OK));test_server_name=NULL;
    // Check the published manifest over independently verified HTTPS as well.
    if(strcmp(argv[3],"skip")){
        char manifest[1024],version[40];assert(!github_update_info(manifest,sizeof(manifest)));
        assert(!mini_json_string(manifest,"version",version,sizeof(version)));assert(!strcmp(version,argv[3]));
    }
    remove(GITHUB_SEED_PATH);remove("/tmp/openhome-github-download");
    puts("GitHub HTTPS, redirect download, checksum rejection, URL restrictions, and version checks passed");
    return 0;
}
