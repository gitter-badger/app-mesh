# Promote native application to microservice application
App Mesh is designed for manage micro service applications, for a native application without any refactor and adapt, can promote as a micro service application with App Mesh, other app can use HTTP request to call this native application.

## Solution
App Mesh support POST [/appmesh/app/run] API used to run a command remotely, with this feature, we can launch native app in server side and get result via REST response, App Mesh framework will guarantee the security and permission. 

The interactive with native application can use std input, App Mesh support pass text (json) data to application process.

## Deploy App Mesh by Docker container
* Assume native app is `/usr/share/myapp.py`, mount native app binary to container.
* Expose 6060 for App Mesh service port
* Start Docker container in backend:
```
docker run -d -m 8g --restart=always -v /usr/share/myapp.py:/usr/share/myapp.py:ro --name=myapp -p 6060:6060 -v /var/run/docker.sock:/var/run/docker.sock laoshanxi/appmesh
```
* If we have any special configuration changes for App Mesh container, we can add `-v /opt/user/appsvc.json:/opt/appmesh/appsvc.json:ro`.
* mount docker.sock to container so that App Mesh will also support manage container app.

## Use native application
### Security
App Mesh by default enable JWT authentication for all REST requests, we need get JWT token:
```
curl -X POST -k -H "username:$(echo -n admin | base64)" -H "password:$(echo -n Admin123 | base64)" https://localhost:6060/appmesh/login
``` 
BTW, the admin user password can be changed by appsvc.json or override with container(laoshanxi/appmesh) startup environment like `-e APPMESH_Security_Users_admin_key=MyNewPwd`

### Call micro service
With JWT token, you can call native app by App Mesh REST now, the body can include you remote application start command and metadata for stdin:
```
curl -X POST -k -H "Authorization:Bearer $JWT_TOKEN" \
-d '{ "command" : "python3 /usr/share/myapp.py", "metadata": "std input text data" }' \
https://appmesh-host:6060/appmesh/app/syncrun?timeout=30
```
You will get result by REST response.
