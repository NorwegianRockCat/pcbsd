// ===============================
//  PC-BSD REST/JSON API Server
// Available under the 3-clause BSD License
// Written by: Ken Moore <ken@pcbsd.org> July 2015
// =================================
#include "WebSocket.h"
#include "syscache-client.h"

#define DEBUG 1
#define SCLISTDELIM QString("::::") //SysCache List Delimiter

WebSocket::WebSocket(QWebSocket *sock, QString ID, AuthorizationManager *auth){
  SockID = ID;
  SockAuthToken.clear(); //nothing set initially
  SOCKET = sock;
  AUTHSYSTEM = auth;
  idletimer = new QTimer(this);
    idletimer->setInterval(600000); //10-minute timeout
    idletimer->setSingleShot(true);
  connect(idletimer, SIGNAL(timeout()), this, SLOT(checkIdle()) );
  connect(SOCKET, SIGNAL(textMessageReceived(const QString&)), this, SLOT(EvaluateMessage(const QString&)) );
  connect(SOCKET, SIGNAL(binaryMessageReceived(const QByteArray&)), this, SLOT(EvaluateMessage(const QByteArray&)) );
  connect(SOCKET, SIGNAL(aboutToClose()), this, SLOT(SocketClosing()) );
  idletimer->start();
}

WebSocket::~WebSocket(){
  if(SOCKET!=0){
    SOCKET->close();
  }
  delete SOCKET;
}


QString WebSocket::ID(){
  return SockID;
}
	
//=======================
//             PRIVATE
//=======================
void WebSocket::EvaluateREST(QString msg){
  //Parse the message into it's elements and proceed to the main data evaluation
  RestInputStruct IN(msg);
  //NOTE: All the REST functionality is disabled for the moment, until we decide to turn it on again at a later time (just need websockets right now - not full REST)	

  if(DEBUG){
    qDebug() << "New REST Message:";
    qDebug() << "  VERB:" << IN.VERB << "URI:" << IN.URI;
    qDebug() << "  HEADERS:" << IN.Header;
    qDebug() << "  BODY:" << IN.Body;
  }
  //Now check for the REST-specific verbs/actions
  if(IN.VERB == "OPTIONS" || IN.VERB == "HEAD"){
    RestOutputStruct out;	  
      out.CODE = RestOutputStruct::OK;
      if(IN.VERB=="HEAD"){
	
      }else{ //OPTIONS
	out.Header << "Allow: HEAD, GET";
	out.Header << "Hosts: /syscache";	      
      }
      out.Header << "Accept: text/json";
      out.Header << "Content-Type: text/json; charset=utf-8";
    SOCKET->sendTextMessage(out.assembleMessage());
  }else{ 
    EvaluateRequest(IN);
  }
}

void WebSocket::EvaluateRequest(const RestInputStruct &REQ){
  RestOutputStruct out;
  if(REQ.VERB != "GET"){
    //Non-supported request (at the moment) - return an error message
    out.CODE = RestOutputStruct::BADREQUEST;
  }else{
    //GET request
    //Now check the body of the message and do what it needs
    QJsonDocument doc = QJsonDocument::fromJson(REQ.Body.toUtf8());
    if(doc.isNull()){ qWarning() << "Empty JSON Message Body!!" << REQ.Body.toUtf8(); }
    //Define the output structures
    QJsonObject ret; //return message

    //Objects contain other key/value pairs - this is 99% of cases
    if(doc.isObject()){
      //First check/set all the various required fields (both in and out)
      bool good = doc.object().contains("namespace") \
	    && doc.object().contains("name") \
	    && doc.object().contains("id") \
	    && doc.object().contains("args");
      //Can add some fallbacks for missing fields here - but not implemented yet
	    
      //parse the message and do something
      if(good && (JsonValueToString(doc.object().value("namespace"))=="rpc") ){
	//Now fetch the outputs from the appropriate subsection
	//Note: Each subsection needs to set the "name", "namespace", and "args" output objects
	QString name = JsonValueToString(doc.object().value("name")).toLower();
	QJsonValue args = doc.object().value("args");
	if(name.startsWith("auth")){
	  //Now perform authentication based on type of auth given
	  //Note: This sets/changes the current SockAuthToken
	  AUTHSYSTEM->clearAuth(SockAuthToken); //new auth requested - clear any old token
	  if(DEBUG){ qDebug() << "Authenticate Peer:" << SOCKET->peerAddress().toString(); }
	  bool localhost = (SOCKET->peerAddress() == QHostAddress::LocalHost) || (SOCKET->peerAddress() == QHostAddress::LocalHostIPv6);
	  //Now do the auth
	  if(name=="auth" && args.isObject() ){
	    //username/password authentication
	    QString user, pass;
	    if(args.toObject().contains("username")){ user = JsonValueToString(args.toObject().value("username"));  }
	    if(args.toObject().contains("password")){ user = JsonValueToString(args.toObject().value("password"));  }
	    SockAuthToken = AUTHSYSTEM->LoginUP(localhost, user, pass);
	  }else if(name == "auth_token" && args.isObject()){
	    SockAuthToken = JsonValueToString(args.toObject().value("token"));
	  }else if(name == "auth_service" && args.isObject()){
	    SockAuthToken = AUTHSYSTEM->LoginService(localhost, JsonValueToString(args.toObject().value("service")) );
	  }
	  
	  //Now check the auth and respond appropriately
	  if(AUTHSYSTEM->checkAuth(SockAuthToken)){
	    //Good Authentication - return the new token 
	    ret.insert("namespace", QJsonValue("rpc"));
	    ret.insert("name", QJsonValue("response"));
	    ret.insert("id", doc.object().value("id")); //use the same ID for the return message
	    QJsonArray array;
	      array.append(SockAuthToken);
	      array.append(AUTHSYSTEM->checkAuthTimeoutSecs(SockAuthToken));
	    ret.insert("args", array);
	  }else{
	    SockAuthToken.clear(); //invalid token
	    //Bad Authentication - return error
	    SetOutputError(&ret, JsonValueToString(doc.object().value("id")), 401, "Unauthorized");
	  }
		
	}else if( AUTHSYSTEM->checkAuth(SockAuthToken) ){ //validate current Authentication token	 
	  //Now provide access to the various subsystems
	  //Pre-set any output fields
          QJsonObject outargs;	
	    ret.insert("namespace", QJsonValue("rpc"));
	    ret.insert("name", QJsonValue("response"));
	    ret.insert("id", doc.object().value("id")); //use the same ID for the return message
		
          if(name == "syscache"){
            EvaluateSysCacheRequest(doc.object().value("args"), &outargs);
	  }
          ret.insert("args",outargs);	  
        }else{
	  //Bad/No authentication
	  SetOutputError(&ret, JsonValueToString(doc.object().value("id")), 401, "Unauthorized");
	}
	      
      }else{
        //Error in inputs - assemble the return error message
	QString id = "error";
	if(doc.object().contains("id")){ id = JsonValueToString(doc.object().value("id")); } //use the same ID
	SetOutputError(&ret, id, 400, "Bad Request");
      }
    }else{
      //Unknown type of JSON input - nothing to do
    }
    //Assemble the outputs for this "GET" request
    out.CODE = RestOutputStruct::OK;
      //Assemble the output JSON document/text
      QJsonDocument retdoc; 
      retdoc.setObject(ret);
    out.Body = retdoc.toJson();
    out.Header << "Content-Type: text/json; charset=utf-8";
  }
  //Return any information
  SOCKET->sendTextMessage(out.assembleMessage());
}

// === SYSCACHE REQUEST INTERACTION ===
void WebSocket::EvaluateSysCacheRequest(const QJsonValue args, QJsonObject *out){
  QJsonObject obj; //output object
  if(args.isObject()){
    //For the moment: all arguments are full syscache DB calls - no special ones
    QStringList reqs = args.toObject().keys();
    if(!reqs.isEmpty()){
      if(DEBUG){ qDebug() << "Parsing Inputs:" << reqs; }
      for(int r=0; r<reqs.length(); r++){
        QString req =  JsonValueToString(args.toObject().value(reqs[r]));
        if(DEBUG){ qDebug() << "  ["+reqs[r]+"]="+req; }
        QStringList values = SysCacheClient::parseInputs( QStringList() << req ); 
        values.removeAll("");
        //Quick check if a list of outputs was returned
        if(values.length()==1){
          values = values[0].split(SCLISTDELIM); //split up the return list (if necessary)
          values.removeAll("");
        }
        if(DEBUG){ qDebug() << " - Returns:" << values; }
        if(values.length()<2){ out->insert(req, QJsonValue(values.join("")) ); }
        else{
          //This is an array of outputs
          QJsonArray arr;
          for(int i=0; i<values.length(); i++){ arr.append(values[i]); }
          out->insert(req,arr);
        }
      }
    } //end of special "request" objects
  }else if(args.isArray()){
    QStringList inputs = JsonArrayToStringList(args.toArray());
    if(DEBUG){ qDebug() << " syscache inputs:" << inputs; }
    QStringList values = SysCacheClient::parseInputs(inputs );
    for(int i=0; i<values.length(); i++){
      if(values[i].contains(SCLISTDELIM)){
	  //This is an array of values
	  QStringList vals = values[i].split(SCLISTDELIM);
	  vals.removeAll("");
	  QJsonArray arr;
	    for(int j=0; j<vals.length(); j++){ arr.append(vals[j]); }
	    out->insert(inputs[i],arr);
      }else{
          out->insert(inputs[i],values[i]);
      }
    }
  } //end array of inputs

}


// === GENERAL PURPOSE UTILITY FUNCTIONS ===
QString WebSocket::JsonValueToString(QJsonValue val){
  //Note: Do not use this on arrays - only use this on single-value values
  QString out;
  switch(val.type()){
    case QJsonValue::Bool:
	out = (val.toBool() ? "true": "false"); break;
    case QJsonValue::Double:
	out = QString::number(val.toDouble()); break;
    case QJsonValue::String:
	out = val.toString(); break;
    case QJsonValue::Array:
	out = "\""+JsonArrayToStringList(val.toArray()).join("\" \"")+"\"";
    default:
	out.clear();
  }
  return out;
}

QStringList WebSocket::JsonArrayToStringList(QJsonArray array){
  //Note: This assumes that the array is only values, not additional objects
  QStringList out;
  qDebug() << "Array to List:" << array.count();
  for(int i=0; i<array.count(); i++){
    out << JsonValueToString(array.at(i));
  }
  return out;  
}

void WebSocket::SetOutputError(QJsonObject *ret, QString id, int err, QString msg){
  ret->insert("namespace", QJsonValue("rpc"));
  ret->insert("name", QJsonValue("error"));
  ret->insert("id",QJsonValue(id));
  QJsonObject obj;
  	obj.insert("code", err);
	obj.insert("message", QJsonValue(msg));
  ret->insert("args",obj);	
}

// =====================
//       PRIVATE SLOTS
// =====================
void WebSocket::checkIdle(){
  //This function is called automatically every few seconds that a client is connected
  if(SOCKET !=0){
    qDebug() << " - Client Timeout: Closing connection...";
    SOCKET->close(); //timeout - close the connection to make way for others
  }
}

void WebSocket::SocketClosing(){
  qDebug() << "Socket Closing...";
  if(idletimer->isActive()){ 
    //This means the client deliberately closed the connection - not the idle timer
    idletimer->stop(); 
    //Clear the current authentication token
    AUTHSYSTEM->clearAuth(SockAuthToken);
  }
  //Stop any current requests

  //Reset the pointer
  SOCKET = 0;	
  emit SocketClosed(SockID);
}

void WebSocket::EvaluateMessage(const QByteArray &msg){
  qDebug() << "New Binary Message:";
  if(idletimer->isActive()){ idletimer->stop(); }
  EvaluateREST( QString(msg) );
  idletimer->start(); 
  qDebug() << "Done with Message";
}

void WebSocket::EvaluateMessage(const QString &msg){ 
  qDebug() << "New Text Message:";
  if(idletimer->isActive()){ idletimer->stop(); }
  EvaluateREST(msg);
  idletimer->start(); 
  qDebug() << "Done with Message";
}