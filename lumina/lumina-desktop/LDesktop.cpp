//===========================================
//  Lumina-DE source code
//  Copyright (c) 2012, Ken Moore
//  Available under the 3-clause BSD license
//  See the LICENSE file for full details
//===========================================
#include "LDesktop.h"

LDesktop::LDesktop(int deskNum) : QObject(){
	
  DPREFIX = "desktop-"+QString::number(deskNum)+"/";
  desktopnumber = deskNum;
  desktop = new QDesktopWidget();
  defaultdesktop = (deskNum== desktop->primaryScreen());
  xoffset = 0;
  for(int i=0; i<desktopnumber; i++){
    xoffset += desktop->screenGeometry(i).width();
  }
  deskMenu = new QMenu(0);
    connect(deskMenu, SIGNAL(triggered(QAction*)), this, SLOT(SystemApplication(QAction*)) );
  appmenu = new AppMenu(0);
  workspacelabel = new QLabel(0);
    workspacelabel->setAlignment(Qt::AlignCenter);
  wkspaceact = new QWidgetAction(0);
    wkspaceact->setDefaultWidget(workspacelabel);
  //Setup the internal variables
  settings = new QSettings(QSettings::UserScope, "LuminaDE","desktopsettings", this);
  //qDebug() << " - Desktop Settings File:" << settings->fileName();
  if(!QFile::exists(settings->fileName())){ settings->setValue(DPREFIX+"background/filelist",QStringList()<<"default"); settings->sync(); }
  bgtimer = new QTimer(this);
    bgtimer->setSingleShot(true);
    connect(bgtimer, SIGNAL(timeout()), this, SLOT(UpdateBackground()) );
  watcher = new QFileSystemWatcher(this);
    connect(watcher, SIGNAL(fileChanged(QString)), this, SLOT(SettingsChanged()) );
    watcher->addPath(settings->fileName());
 
  bgWindow = new QWidget(0);
	bgWindow->setObjectName("bgWindow");
	bgWindow->setContextMenuPolicy(Qt::CustomContextMenu);
	LX11::SetAsDesktop(bgWindow->winId());
	bgWindow->setGeometry(xoffset,0,desktop->screenGeometry().width(), desktop->screenGeometry().height());
	connect(bgWindow, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(ShowMenu()) );
  
  //Start the update processes
  QTimer::singleShot(1,this, SLOT(UpdateMenu()) );
  QTimer::singleShot(1,this, SLOT(UpdateBackground()) );
  QTimer::singleShot(1,this, SLOT(UpdateDesktop()) );
  QTimer::singleShot(1,this, SLOT(UpdatePanels()) );

}

LDesktop::~LDesktop(){
  delete deskMenu;
  delete appmenu;
  delete bgWindow;
  delete workspacelabel;
  delete wkspaceact;
}

void LDesktop::SystemTerminal(){ 
  QString term = settings->value("default-terminal","xterm").toString();
  QProcess::startDetached(term); 
}

void LDesktop::SystemFileManager(){
  QProcess::startDetached("lumina-fm");	
}

void LDesktop::SystemApplication(QAction* act){
  if(!act->whatsThis().isEmpty()){
    QProcess::startDetached("lumina-open \""+act->whatsThis()+"\"");
  }
}

// =====================
//     PRIVATE SLOTS 
// =====================
void LDesktop::SettingsChanged(){
  settings->sync(); //make sure to catch external settings changes
  QTimer::singleShot(1,this, SLOT(UpdateMenu()) );
  QTimer::singleShot(1,this, SLOT(UpdateBackground()) );
  QTimer::singleShot(1,this, SLOT(UpdateDesktop()) );
  QTimer::singleShot(1,this, SLOT(UpdatePanels()) );
}

void LDesktop::UpdateMenu(bool fast){
  //qDebug() << " - Update Menu:" << desktopnumber;
  //Put a label at the top 
  int num = LX11::GetCurrentDesktop();
  qDebug() << "Found desktop number:" << num;
  if(num < 0){ workspacelabel->setText( "<b>"+tr("Lumina Desktop")+"</b>"); }
  else{ workspacelabel->setText( "<b>"+QString(tr("Workspace %1")).arg(QString::number(num+1))+"</b>"); }
  if(fast){ return; } //already done
  deskMenu->clear(); //clear it for refresh
  deskMenu->addAction(wkspaceact);
  deskMenu->addSeparator();
  //Now load the user's menu setup and fill the menu
  QStringList items = settings->value("menu/itemlist", QStringList()<< "terminal" << "filemanager" <<"applications" << "line" << "settings" ).toStringList();
  for(int i=0; i<items.length(); i++){
    if(items[i]=="terminal"){ deskMenu->addAction(LXDG::findIcon("utilities-terminal",""), tr("Terminal"), this, SLOT(SystemTerminal()) ); }
    else if(items[i]=="filemanager"){ deskMenu->addAction( LXDG::findIcon("system-file-manager",""), tr("Browse System"), this, SLOT(SystemFileManager()) ); }
    else if(items[i]=="applications"){ deskMenu->addMenu( LSession::applicationMenu() ); }
    else if(items[i]=="line"){ deskMenu->addSeparator(); }
    else if(items[i]=="settings"){ deskMenu->addMenu( LSession::settingsMenu() ); }
    else if(items[i].startsWith("app::::") && items[i].endsWith(".desktop")){
      //Custom *.desktop application
      QString file = items[i].section("::::",1,1).simplified();
      bool ok = false;
      XDGDesktop xdgf = LXDG::loadDesktopFile(file, ok);
      if(ok){
        deskMenu->addAction( LXDG::findIcon(xdgf.icon,""), xdgf.name)->setWhatsThis(file);
	}else{
	  qDebug() << "Could not load application file:" << file;
	}
    }
  }
  //Now add the system quit options
  deskMenu->addSeparator();
  deskMenu->addAction(LXDG::findIcon("system-log-out",""), tr("Log Out"), this, SLOT(SystemLogout()) );
}

void LDesktop::UpdateDesktop(){
  qDebug() << " - Update Desktop:" << desktopnumber;
  QStringList plugins = settings->value(DPREFIX+"pluginlist", QStringList()).toStringList();
  /*if(defaultdesktop && plugins.isEmpty()){
    plugins << "desktopview";
  }*/
  for(int i=0; i<plugins.length(); i++){
    //See if this plugin is already there
    LDPlugin *plug = 0;
    for(int p=0; p<PLUGINS.length(); p++){
      if(PLUGINS[p]->type()==plugins[i]){
	plug = PLUGINS[p];
	break;
      }
    }
    if(plug==0){
      //New Plugin
      qDebug() << " -- New Plugin:" << plugins[i];
      plug = NewDP::createPlugin(plugins[i], bgWindow);
      if(plug != 0){ 
        PLUGINS << plug;
      }
    }
    //Update the plugin geometry
    if(plug!=0){
      QString geom = settings->value(DPREFIX+plugins[i]+"/geometry", "").toString();
      if(geom.isEmpty()){
	if(plugins.length()==1 && plugins[i]=="desktopview"){ 
	  geom = "0,0,"+QString::number(desktop->availableGeometry().width())+","+QString::number(desktop->availableGeometry().height());
	}else{
	  geom = QString::number(i*200)+",0,200,"+QString::number(desktop->availableGeometry().height());
	}
      }
      plug->setGeometry(geom.section(",",0,0).toInt(),geom.section(",",1,1).toInt(), geom.section(",",2,2).toInt(), geom.section(",",3,3).toInt() );
      plug->show(); //make sure it is visible
    }
  }
  
}

void LDesktop::UpdatePanels(){
  qDebug() << " - Update Panels:" << desktopnumber;
  int panels = settings->value(DPREFIX+"panels", -1).toInt();
  if(panels==-1 && defaultdesktop){ panels=1; } //need at least 1 panel on the primary desktop
  //Remove all extra panels
  for(int i=0; i<PANELS.length(); i++){
    if(panels <= PANELS[i]->number()){
      delete PANELS.takeAt(i);
      i--;
    }
  }
  for(int i=0; i<panels; i++){
    //Check for a panel with this number
    bool found = false;
    for(int p=0; p<PANELS.length(); p++){
      if(PANELS[p]->number() == i){
        found = true;
	qDebug() << " -- Update panel "<< i;
        //panel already exists - just update it
        QTimer::singleShot(1, PANELS[i], SLOT(UpdatePanel()) );      
      }
    }
    if(!found){
      qDebug() << " -- Create panel "<< i;
      //New panel
      PANELS << new LPanel(settings, desktopnumber, i);
    }
  }
  
  
}

void LDesktop::UpdateBackground(){
  //Get the current Background
  qDebug() << " - Update Background:" << desktopnumber;
  //Get the list of background(s) to show
  QStringList bgL = settings->value(DPREFIX+"background/filelist", QStringList()).toStringList();
  //qDebug() << " - List:" << bgL << CBG;
    //Remove any invalid files
    for(int i=0; i<bgL.length(); i++){ 
      if( (!QFile::exists(bgL[i]) && bgL[i]!="default") || bgL[i].isEmpty()){ bgL.removeAt(i); i--; } 
    }
  //Determine which background to use next
  int index = bgL.indexOf(CBG);
  if( (index < 0) || (index >= bgL.length()-1) ){ index = 0; } //use the first file
  else{ index++; } //use the next file in the list
  QString bgFile;
  if( bgL.isEmpty() && CBG.isEmpty()){ bgFile = "default"; }
  else if( bgL.isEmpty() && QFile::exists(CBG) ){ bgFile = CBG; }
  else if( bgL.isEmpty() ){ bgFile = "default"; }
  else{ bgFile = bgL[index]; }
  //Save this file as the current background
  CBG = bgFile;
  qDebug() << " - Set Background to:" << CBG << index << bgL;
  if( (bgFile.toLower()=="default")){ bgFile = "/usr/local/share/Lumina-DE/desktop-background.jpg"; }
  //Now set this file as the current background
  QString style = "QWidget#bgWindow{ border-image:url(%1) stretch;}";
  style = style.arg(bgFile);
  bgWindow->setStyleSheet(style);
  bgWindow->show();
  //Now reset the timer for the next change (if appropriate)
  if(bgtimer->isActive()){ bgtimer->stop(); }
  if(bgL.length() > 1){
    //get the length of the timer (in minutes)
    int min = settings->value(DPREFIX+"background/minutesToChange",5).toInt();
    //restart the internal timer
    if(min > 0){
      bgtimer->start(min*60000); //convert from minutes to milliseconds
    }
  }
}
