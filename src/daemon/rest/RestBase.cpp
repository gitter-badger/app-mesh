#include <boost/algorithm/string_regex.hpp>

#include "../../common/Utility.h"
#include "../../common/jwt-cpp/jwt.h"
#include "../Configuration.h"
#include "../security/User.h"
#include "HttpRequest.h"
#include "RestBase.h"
#include "RestChildObject.h"

RestBase::RestBase(bool forward2TcpServer)
    : m_forward2TcpServer(forward2TcpServer)
{
}

RestBase::~RestBase()
{
}

bool RestBase::forwardRestRequest(const HttpRequest &message)
{
    // file download/upload do not forward to server
    if (m_forward2TcpServer && !Utility::startWith(message.m_relative_uri, "/appmesh/file"))
    {
        RestChildObject::instance()->sendRequest2Server(message);
        return true;
    }
    return false;
}

void RestBase::handle_get(const HttpRequest &message)
{
    if (!forwardRestRequest(message))
    {
        handleRest(message, m_restGetFunctions);
    }
}

void RestBase::handle_put(const HttpRequest &message)
{
    if (!forwardRestRequest(message))
    {
        handleRest(message, m_restPutFunctions);
    }
}

void RestBase::handle_post(const HttpRequest &message)
{
    if (!forwardRestRequest(message))
    {
        handleRest(message, m_restPstFunctions);
    }
}

void RestBase::handle_delete(const HttpRequest &message)
{
    if (!forwardRestRequest(message))
    {
        handleRest(message, m_restDelFunctions);
    }
}

void RestBase::handle_options(const HttpRequest &message)
{
    message.reply(web::http::status_codes::OK);
}

void RestBase::handleRest(const HttpRequest &message, const std::map<std::string, std::function<void(const HttpRequest &)>> &restFunctions)
{
    const static char fname[] = "RestHandler::handleRest() ";
    REST_INFO_PRINT;
    std::function<void(const HttpRequest &)> stdFunction;
    const auto path = Utility::stringReplace(message.m_relative_uri, "//", "/");

    if (path == "/" || path.empty())
    {
        message.reply(status_codes::OK, "App Mesh");
        return;
    }

    bool findRest = false;
    for (const auto &kvp : restFunctions)
    {
        if (path == kvp.first || boost::regex_match(path, boost::regex(kvp.first)))
        {
            findRest = true;
            stdFunction = kvp.second;
            break;
        }
    }
    if (!findRest)
    {
        message.reply(status_codes::NotFound, "Path not found");
        return;
    }

    try
    {
        stdFunction(message);
    }
    catch (const std::exception &e)
    {
        LOG_WAR << fname << "rest " << path << " failed with error: " << e.what();
        message.reply(web::http::status_codes::BadRequest, e.what());
    }
    catch (...)
    {
        LOG_WAR << fname << "rest " << path << " failed";
        message.reply(web::http::status_codes::BadRequest, "unknow exception");
    }
}

void RestBase::bindRestMethod(const web::http::method &method, const std::string &path, std::function<void(const HttpRequest &)> func)
{
    const static char fname[] = "RestHandler::bindRest() ";

    LOG_DBG << fname << "bind " << method << " for " << path;

    // bind to map
    if (method == web::http::methods::GET)
        m_restGetFunctions[path] = func;
    else if (method == web::http::methods::PUT)
        m_restPutFunctions[path] = func;
    else if (method == web::http::methods::POST)
        m_restPstFunctions[path] = func;
    else if (method == web::http::methods::DEL)
        m_restDelFunctions[path] = func;
    else
        LOG_ERR << fname << method << " not supported.";
}

const std::string RestBase::getJwtToken(const HttpRequest &message)
{
    std::string token;
    if (message.m_headers.count(HTTP_HEADER_JWT_Authorization))
    {
        token = Utility::stdStringTrim(message.m_headers.find(HTTP_HEADER_JWT_Authorization)->second);
        const std::string bearerFlag = HTTP_HEADER_JWT_BearerSpace;
        if (Utility::startWith(token, bearerFlag))
        {
            token = token.substr(bearerFlag.length());
        }
    }
    return token;
}

const std::string RestBase::createJwtToken(const std::string &uname, const std::string &passwd, int timeoutSeconds)
{
    if (uname.empty() || passwd.empty())
    {
        throw std::invalid_argument("must provide name and password to generate token");
    }

    // https://thalhammer.it/projects/
    // https://www.cnblogs.com/mantoudev/p/8994341.html
    // 1. Header {"typ": "JWT","alg" : "HS256"}
    // 2. Payload{"iss": "appmesh-auth0","name" : "u-name",}
    // 3. Signature HMACSHA256((base64UrlEncode(header) + "." + base64UrlEncode(payload)), 'secret');
    // creating a token that will expire in one hour
    const auto token = jwt::create()
                           .set_issuer(HTTP_HEADER_JWT_ISSUER)
                           .set_type(HTTP_HEADER_JWT)
                           .set_issued_at(jwt::date(std::chrono::system_clock::now()))
                           .set_expires_at(jwt::date(std::chrono::system_clock::now() + std::chrono::seconds{timeoutSeconds}))
                           .set_payload_claim(HTTP_HEADER_JWT_name, jwt::claim(uname))
                           .sign(jwt::algorithm::hs256{passwd});
    return token;
}

const std::string RestBase::verifyToken(const HttpRequest &message)
{
    if (!Configuration::instance()->getJwtEnabled())
        return "";

    const auto token = getJwtToken(message);
    const auto decoded_token = jwt::decode(token);
    if (decoded_token.has_payload_claim(HTTP_HEADER_JWT_name))
    {
        // get user info
        const auto userName = decoded_token.get_payload_claim(HTTP_HEADER_JWT_name);
        const auto userObj = Configuration::instance()->getUserInfo(userName.as_string());
        const auto userKey = userObj->getKey();

        // check locked
        if (userObj->locked())
            throw std::invalid_argument(Utility::stringFormat("User <%s> was locked", userName.as_string().c_str()));

        // check user token
        auto verifier = jwt::verify()
                            .allow_algorithm(jwt::algorithm::hs256{userKey})
                            .with_issuer(HTTP_HEADER_JWT_ISSUER)
                            .with_claim(HTTP_HEADER_JWT_name, userName);
        verifier.verify(decoded_token);

        return std::move(userName.as_string());
    }
    else
    {
        throw std::invalid_argument("No user info in token");
    }
}

const std::string RestBase::getJwtUserName(const HttpRequest &message)
{
    if (!Configuration::instance()->getJwtEnabled())
        return std::string();

    const auto token = getJwtToken(message);
    const auto decoded_token = jwt::decode(token);
    if (decoded_token.has_payload_claim(HTTP_HEADER_JWT_name))
    {
        // get user info
        return decoded_token.get_payload_claim(HTTP_HEADER_JWT_name).as_string();
    }
    else
    {
        throw std::invalid_argument("No user info in token");
    }
}

bool RestBase::permissionCheck(const HttpRequest &message, const std::string &permission)
{
    const static char fname[] = "RestHandler::permissionCheck() ";

    const auto userName = verifyToken(message);
    if (permission.length() && userName.length() && Configuration::instance()->getJwtEnabled())
    {
        // check user role permission
        if (Configuration::instance()->getUserPermissions(userName).count(permission))
        {
            LOG_DBG << fname << "authentication success for remote: " << message.m_remote_address << " with user : " << userName << " and permission : " << permission;
            return true;
        }
        else
        {
            LOG_WAR << fname << "No such permission " << permission << " for user " << userName;
            throw std::invalid_argument(Utility::stringFormat("No permission <%s> for user <%s>", permission.c_str(), userName.c_str()));
        }
    }
    else
    {
        // JWT not enabled
        return true;
    }
}