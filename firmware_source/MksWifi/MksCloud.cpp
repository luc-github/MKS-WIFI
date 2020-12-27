#include "Config.h"
#include "MksCloud.h"

boolean cloud_enable_flag = false;
int cloud_link_state = 0;
char cloud_file_id[40];
char cloud_user_id[40];
char cloud_file_url[96];
char unbind_exec = 0;

WiFiClient cloud_client;

//TODO Cloud functions if any
