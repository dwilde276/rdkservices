/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2019 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
**/

/**
 *  Utility functions used in plugins.
 *
 */

#include <string.h>
#include <sstream>
#include "utils.h"
#include "libIBus.h"
#include <securityagent/SecurityTokenUtil.h>
#include <curl/curl.h>
#include <utility>
#include <ctype.h>
#include <mutex>

#define MAX_STRING_LENGTH 2048

using namespace WPEFramework;
using namespace std;

const char* Utils::IARM::NAME = "Thunder_Plugins";

bool Utils::IARM::isConnected() {
    IARM_Result_t res;
    int isRegistered = 0;
    res = IARM_Bus_IsConnected(NAME, &isRegistered);
    LOGINFO("IARM_Bus_IsConnected: %d (%d)", res, isRegistered);

    return (isRegistered == 1);
}

bool Utils::IARM::init() {
    IARM_Result_t res;
    bool result = false;

    if (isConnected()) {
        LOGINFO("IARM already connected");
        result = true;
    } else {
        res = IARM_Bus_Init(NAME);
        LOGINFO("IARM_Bus_Init: %d", res);
        if (res == IARM_RESULT_SUCCESS ||
            res == IARM_RESULT_INVALID_STATE /* already inited or connected */) {

            res = IARM_Bus_Connect();
            LOGINFO("IARM_Bus_Connect: %d", res);
            if (res == IARM_RESULT_SUCCESS ||
                res == IARM_RESULT_INVALID_STATE /* already connected or not inited */) {

                result = isConnected();
            } else {
                LOGERR("IARM_Bus_Connect failure: %d", res);
            }
        } else {
            LOGERR("IARM_Bus_Init failure: %d", res);
        }
    }

    return result;
}

std::string Utils::formatIARMResult(IARM_Result_t result)
{
    switch (result) {
        case IARM_RESULT_SUCCESS:       return std::string("IARM_RESULT_SUCCESS [success]");
        case IARM_RESULT_INVALID_PARAM: return std::string("IARM_RESULT_INVALID_PARAM [invalid input parameter]");
        case IARM_RESULT_INVALID_STATE: return std::string("IARM_RESULT_INVALID_STATE [invalid state encountered]");
        case IARM_RESULT_IPCCORE_FAIL:  return std::string("IARM_RESULT_IPCORE_FAIL [underlying IPC failure]");
        case IARM_RESULT_OOM:           return std::string("IARM_RESULT_OOM [out of memory]");
        default:
            std::ostringstream tmp;
            tmp << result << " [unknown IARM_Result_t]";
            return tmp.str();
    }
}

/***
 * @brief	: Execute shell script and get response
 * @param1[in]	: script to be executed with args
 * @return		: string; response.
 */
std::string Utils::cRunScript(const char *cmd)
{
    std::string totalStr = "";
    FILE *pipe = NULL;
    char buff[1024] = {'\0'};

    if ((pipe = popen(cmd, "r"))) {
        memset(buff, 0, sizeof(buff));
        while (fgets(buff, sizeof(buff), pipe)) {
            totalStr += buff;
            memset(buff, 0, sizeof(buff));
        }
        pclose(pipe);
    } else {
        /* popen failed. */
    }
    return totalStr;
}

using namespace WPEFramework;

/***
 * @brief	: Checks that file exists
 * @param1[in]	: pFileName name of file
 * @return		: true if file exists.
 */
bool Utils::fileExists(const char *pFileName)
{
    struct stat fileStat;
    return 0 == stat(pFileName, &fileStat);
}

/***
 * @brief	: Checks that file exists and modified at least pointed seconds ago
 * @param1[in]	: pFileName name of file
 * @param1[in]	: age modification age in seconds
 * @return		: true if file exists and modifies 'age' seconds ago.
 */
bool Utils::isFileExistsAndOlderThen(const char *pFileName, long age /*= -1*/)
{
    struct stat fileStat;
    int res = stat(pFileName, &fileStat);
    if (0 != res)
        return false;

    if (-1 == age)
        return true;

    time_t currentTime = time(nullptr);
    //LOGWARN("current time of %s: %lu", pFileName, currentTime);

    time_t modifiedSecondsAgo = difftime(currentTime, fileStat.st_mtime);
    //LOGWARN("elapsed time is %lu, %s", modifiedSecondsAgo, modifiedSecondsAgo <= age ? "updated recently (doesn't exists)" : "updated long time ago (exists)");

    return modifiedSecondsAgo > age;
}

bool Utils::Port::isJsonMatch(std::string inString) 
{
    return std::regex_match(inString, "(sub)(.*)");
}

std::string Utils::Port::retrieveJsonValue(std::string stringLine, std::string rgxMatch)
{
    std::smatch matchFound;
    std::regex rgx(rgxMatch);
    if (std::regex_search(stringLine.begin(), stringLine.end(), matchFound, rgx)) {
        return matchFound; // SUCCEED!
    } else {
        std::string fouledUp "NO MATCH on '" += rgxMatch;
        fubar += "'";
        return fouledUp; // FAIL!
    }
}

std::string Utils::Port::fetchCurrentIpBindingAndPort()
{
    std::fstream inFileStream;
    std::string jsonLine; // each line of JSON file
    uint8_t success = 0; // count 0 up to 2 for success on all matches!
    std::string ip_binding_n_port = ""; // start with empty string
    std::string hardcoded_ip_n_port = "127.0.0.1:9998"; // fall back to old hard-coded value
    std::string ourWpeFwConfig = "/etc/WPEFramework/config.json";

    if (Utils::fileExists(ourWpeFwConfig)) {
        inFileStream.open(ourWpeFwConfig, ios::in);
        if (inFileStream.is_open()) {
            while(getline(inFileStream, jsonLine)) {
                if Utils::Port::isJsonMatch("\"binding\"") {
                    // Note that this is stored as an String in the JSON
                    // Handles corner case where there is a space after the colon
                    ip_binding_n_port += Utils::Port::retrieveJsonValue(jsonLine, ":\s?\"([0-9.]+)\"");
                    success++;
                } else {
                    //LOGWARN("/etc/WPEFramework/config.json does not have a 'binding' entry!");
                }
                if Utils::Port::isJsonMatch("\"port\"") {
                    // Note that this is stored as an Integer in the JSON
                    // Handles corner case where there is a space after the colon
                    ip_binding_n_port += Utils::Port::retrieveJsonValue(jsonLine, ":\s?([0-9]+");
                    success++;
                } else {
                    //LOGWARN("/etc/WPEFramework/config.json does not have a 'port' entry!");
                }
            }
        }
        inFileStream.close(); // done searching!
        if (success == 2) {
            return ip_binding_n_port; // @return : 'ip.add.re.ss:port'
        } else {
            //LOGWARN("At least one of the search patterns was missing or corrupted!");
            return hardcoded_ip_n_port; // FAIL!
        }
    } else {
        //LOGWARN("'/etc/WPEFramework/config.json' does not exist yet!");
        return hardcoded_ip_n_port; // FAIL!
    }
}

void Utils::SecurityToken::getSecurityToken(std::string& token)
{
    static std::mutex mtx;
    std::unique_lock<std::mutex> lock(mtx);

    if(m_sThunderSecurityChecked)
    {
        token = m_sToken;
        return;
    }

    // Thunder Security is enabled by Default.
    bool thunderSecurityRFCEnabled = true;
    RFC_ParamData_t param;
    if (getRFCConfig("Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.ThunderSecurity.Enable", param))
    {
        if (param.type == WDMP_BOOLEAN && (strncasecmp(param.value,"false",5) == 0))
        {
            thunderSecurityRFCEnabled = false;
        }
    }
    std::cout << "Thunder Security RFC enabled: " << thunderSecurityRFCEnabled << std::endl;
    if(!isThunderSecurityConfigured() || !thunderSecurityRFCEnabled)
    {
        m_sThunderSecurityChecked = true;
        std::cout << "Thunder Security is not enabled. Not getting token\n";
        return;
    }
    m_sThunderSecurityChecked = true;
    unsigned char buffer[MAX_STRING_LENGTH] = {0};

    int ret = GetSecurityToken(MAX_STRING_LENGTH,buffer);
    if(ret < 0)
    {
        std::cout << "Error in getting token\n";
    }
    else
    {
        std::cout << "retrieved token successfully\n";
        token = (char*)buffer;
        m_sToken = token;
    }
}

static size_t writeCurlResponse(void *ptr, size_t size, size_t nmemb, string stream)
{
    size_t realsize = size * nmemb;
    string temp(static_cast<const char*>(ptr), realsize);
    stream.append(temp);
    return realsize;
}

bool Utils::SecurityToken::isThunderSecurityConfigured()
{
    bool configured = false;
    long http_code = 0;
    std::string jsonResp;
    CURL *curl_handle = NULL;
    CURLcode res = CURLE_OK;
    curl_handle = curl_easy_init();
    string serialNumber = "";
    string url = "http://127.0.0.1:9998/Service/Controller/Configuration/Controller";
    if (curl_handle &&
        !curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str()) &&
        !curl_easy_setopt(curl_handle, CURLOPT_HTTPGET,1) &&
        !curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1) && //when redirected, follow the redirections
        !curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, writeCurlResponse) &&
        !curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &jsonResp)) {

        res = curl_easy_perform(curl_handle);
        if(curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code) != CURLE_OK)
        {
            std::cout << "curl_easy_getinfo failed\n";
        }
        std::cout << "Thunder Controller Configuration ret: " << res << " http response code: " << http_code << std::endl;
        curl_easy_cleanup(curl_handle);
    }
    else
    {
        std::cout << "Could not perform curl to read Thunder Controller Configuration\n";
    }
    if ((res == CURLE_OK) && (http_code == 200))
    {
        //check for "Security" in response
        JsonObject responseJson = JsonObject(jsonResp);
        if (responseJson.HasLabel("subsystems"))
        {
            const JsonArray subsystemList = responseJson["subsystems"].Array();
            for (int i=0; i<subsystemList.Length(); i++)
            {
                string subsystem = subsystemList[i].String();
                if (subsystem == "Security")
                {
                    configured = true;
                    break;
                }
            }
        }
    }
    return configured;
}

// Thunder plugins communication
std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> > Utils::getThunderControllerClient(std::string callsign)
{
    string token;
    Utils::SecurityToken::getSecurityToken(token);
    string query = "token=" + token;

    Core::SystemInfo::SetEnvironment(_T("THUNDER_ACCESS"), (_T(SERVER_DETAILS)));
    static std::shared_ptr<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> > thunderClient = make_shared<WPEFramework::JSONRPC::LinkType<WPEFramework::Core::JSON::IElement> >(callsign.c_str(), "",false,query);
    return thunderClient;
}

void Utils::activatePlugin(const char* callSign)
{
    JsonObject joParams;
    joParams.Set("callsign",callSign);
    JsonObject joResult;

    if(!isPluginActivated(callSign))
    {
        LOGINFO("Activating %s", callSign);
        uint32_t status = getThunderControllerClient()->Invoke<JsonObject, JsonObject>(2000, "activate", joParams, joResult);
        string strParams;
        string strResult;
        joParams.ToString(strParams);
        joResult.ToString(strResult);
        LOGINFO("Called method %s, with params %s, status: %d, result: %s"
                , "activate"
                , C_STR(strParams)
                , status
                , C_STR(strResult));
        if (status == Core::ERROR_NONE)
        {
            LOGINFO("%s Plugin activation status ret: %d ", callSign, status);
        }
    }
}

bool Utils::isPluginActivated(const char* callSign)
{
    string method = "status@" + string(callSign);
    Core::JSON::ArrayType<PluginHost::MetaData::Service> joResult;
    uint32_t status = getThunderControllerClient()->Get<Core::JSON::ArrayType<PluginHost::MetaData::Service> >(2000, method.c_str(),joResult);
    bool pluginActivated = false;
    if (status == Core::ERROR_NONE)
    {
        LOGINFO("Getting status for callSign %s, result: %s", callSign, joResult[0].JSONState.Data().c_str());
        pluginActivated = joResult[0].JSONState == PluginHost::IShell::ACTIVATED;
    }
    else
    {
        LOGWARN("Getting status for callSign %s, status: %d", callSign, status);
    }

    if(!pluginActivated){
        LOGWARN("Plugin %s is not active", callSign);
    } else {
        LOGINFO("Plugin %s is active ", callSign);
    }
    return pluginActivated;
}

bool Utils::getRFCConfig(char* paramName, RFC_ParamData_t& paramOutput)
{
    WDMP_STATUS wdmpStatus = getRFCParameter("RDKShell", paramName, &paramOutput);
    if (wdmpStatus == WDMP_SUCCESS || wdmpStatus == WDMP_ERR_DEFAULT_VALUE)
    {
        return true;
    }
    return false;
}

std::string Utils::SecurityToken::m_sToken = "";
bool Utils::SecurityToken::m_sThunderSecurityChecked = false;


//Thread RAII
Utils::ThreadRAII::ThreadRAII(std::thread&& t): t(std::move(t)) {}

Utils::ThreadRAII::~ThreadRAII()
{
    try
    {
        if (t.joinable()) 
        {
            t.join();
        }
    }
    catch(const std::system_error& e)
    {
        LOGERR("system_error exception in thread join %s", e.what());
    }
    catch(const std::exception& e)
    {
        LOGERR("exception in thread join %s", e.what());
    }
}

bool Utils::isValidInt(char* x)
{
    bool Checked = true;
    int i = 0;
    do
    {
        //valid digit?
        if (isdigit(x[i]))
        {
            //to the next character
            i++;
            Checked = true;
        }
        else
        {
            //to the next character
            i++;
            Checked = false;
            break;
        }
    } while (x[i] != '\0');
    return Checked;
}

void Utils::syncPersistFile (const string file) {
    FILE * fp = NULL;
    fp = fopen(file.c_str(), "r");
    if (fp == NULL) {
        printf("fopen NULL\n");
        return;
    }
    fflush(fp);
    fsync(fileno(fp));
    fclose(fp);
}

void Utils::persistJsonSettings(const string strFile, const string strKey, const JsonValue& jsValue)
{
    Core::File file;
    file = strFile.c_str();

    file.Open(false);
    if (!file.IsOpen())
        file.Create();

    JsonObject cecSetting;
    cecSetting.IElement::FromFile(file);
    file.Destroy();
    file.Create();
    cecSetting[strKey.c_str()] = jsValue;
    cecSetting.IElement::ToFile(file);

    file.Close();

    //Sync the settings
    Utils::syncPersistFile (strFile);

    return;
}
