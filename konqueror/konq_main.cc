/* This file is part of the KDE project
   Copyright (C) 1998, 1999 Torben Weis <weis@kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

#include <qdir.h>

#include <opApplication.h>
#include <komApplication.h>
#include <komBase.h>

#include <kservices.h>
#include <kregistry.h>
#include <kregfactories.h>
#include <kio_cache.h>

#include <kded_instance.h>
#include <ktrader.h> //for KTraderServiceProvider

#include <kimgio.h>
#include <kapp.h>
#include <klocale.h>
#include <kglobal.h>
#include <kwm.h>
#include <userpaths.h>

#include <qmessagebox.h>

#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>

#include "kfmrun.h"
#include "konq_main.h"
#include "konq_mainwindow.h"
#include "konq_topwidget.h"
#include "konq_mainview.h"
#include "konq_iconview.h"
#include "konq_htmlview.h"
#include "konq_partview.h"
#include "konq_treeview.h"
#include "konq_txtview.h"

void sig_handler( int signum );
void sig_term_handler( int signum );
void sig_pipe_handler( int signum );

bool g_bWithGUI = true;

/**********************************************
 *
 * KonqApplicationIf
 *
 **********************************************/

typedef KOMBoot<KonqApplicationIf> KonqBoot;

KonqApplicationIf::KonqApplicationIf()
{
}

KonqApplicationIf::KonqApplicationIf( const CORBA::BOA::ReferenceData &refdata ) :
  Konqueror::Application_skel( refdata )
{
}

KonqApplicationIf::KonqApplicationIf( CORBA::Object_ptr _obj ) :
  Konqueror::Application_skel( _obj )
{
}

OpenParts::Part_ptr KonqApplicationIf::createPart()
{
  return OpenParts::Part::_duplicate( new KonqMainView );
}

Konqueror::MainWindow_ptr KonqApplicationIf::createMainWindow( const char* url )
{
  KonqMainWindow * mw = new KonqMainWindow( url );
  mw->show(); // ahah, it won't show up, otherwise !
  return Konqueror::MainWindow::_duplicate( mw->konqInterface() );
}

Konqueror::MainView_ptr KonqApplicationIf::createMainView()
{
  return Konqueror::MainView::_duplicate( new KonqMainView );
}

Konqueror::KfmIconView_ptr KonqApplicationIf::createKfmIconView()
{
  return Konqueror::KfmIconView::_duplicate( new KonqKfmIconView );
}

Konqueror::HTMLView_ptr KonqApplicationIf::createHTMLView()
{
  return Konqueror::HTMLView::_duplicate( new KonqHTMLView );
}

Konqueror::KfmTreeView_ptr KonqApplicationIf::createKfmTreeView()
{
  return Konqueror::KfmTreeView::_duplicate( new KonqKfmTreeView );
}

Konqueror::PartView_ptr KonqApplicationIf::createPartView()
{
  return Konqueror::PartView::_duplicate( new KonqPartView );
}

Konqueror::TxtView_ptr KonqApplicationIf::createTxtView()
{
  return Konqueror::TxtView::_duplicate( new KonqTxtView );
}

/**********************************************
 *
 * KonqApp
 *
 **********************************************/

KonqApp::KonqApp( int &argc, char** argv ) :
  OPApplication( argc, argv, "konqueror" )
{
}

KonqApp::~KonqApp()
{
}

void KonqApp::start()
{
  if ( g_bWithGUI )
  {
    (void) new KonqTopWidget();
  }
}


/**********************************************
 *
 * KonqBookmarkManager
 *
 **********************************************/

void KonqBookmarkManager::editBookmarks( const char *_url )
{
  KonqMainWindow *m_pShell = new KonqMainWindow( _url );
  m_pShell->show();
}

/************* Functions called by main ****************/

void testDir( const char *_name, bool showMsg = FALSE )
{
  DIR *dp;
  dp = opendir( _name );
  if ( dp == NULL )
  {
    QString m = _name;
    if ( m.right(1) == "/" )
      m.truncate( m.length() - 1 );
    
    if (showMsg)
      QMessageBox::information( 0, i18n("Information"),
                                i18n("Creating directory:\n") + m );
    ::mkdir( m, S_IRWXU );
  }
  else
    closedir( dp );
}
 
void copyDirectoryFile(const char *filename, const QString& dir, bool force)
{
  if (force || !QFile::exists(dir + "/.directory")) {
    QString cmd;
    cmd.sprintf( "cp %s/konqueror/%s %s/.directory", kapp->kde_datadir().data(),
                 filename, dir.data() );
    system( cmd.data() );
  }
}

/**
 * @return true if this is the first time 
 * konqy is run for the current release
 * WARNING : call only once !!! (second call will always return false !)
 */
bool isNewRelease()
{
    bool bNewRelease = false;
 
    KConfig* config = kapp->getConfig();
    config->setGroup("Version");
    int versionMajor = config->readNumEntry("KDEVersionMajor", 0);
    int versionMinor = config->readNumEntry("KDEVersionMinor", 0);
    int versionRelease = config->readNumEntry("KDEVersionRelease", 0);
 
    if( versionMajor < KDE_VERSION_MAJOR )
        bNewRelease = true;
    else if( versionMinor < KDE_VERSION_MINOR )
             bNewRelease = true;
         else if( versionRelease < KDE_VERSION_RELEASE )
                  bNewRelease = true;
 
    if( bNewRelease ) {
      config->writeEntry("KDEVersionMajor", KDE_VERSION_MAJOR );
      config->writeEntry("KDEVersionMinor", KDE_VERSION_MINOR );
      config->writeEntry("KDEVersionRelease", KDE_VERSION_RELEASE );
      config->sync();
    }

    return bNewRelease;
}

/*
 * @return true if we must copy Templates, i.e. if new release and if the user agrees.
 * or if there are simply no Templates installed at all
 */
bool checkTemplates( bool bNewRelease )
{
    // Test for existing Templates
    bool bTemplates = true;
 
    DIR* dp = 0L; // avoid warning
    dp = opendir( UserPaths::templatesPath() );
    if ( dp == 0L )
      bTemplates = false;
    else
      closedir( dp );
 
    if( bNewRelease && bTemplates )
    {
      int btn = QMessageBox::information( 0,
                i18n("Information"),
                i18n("A new KDE version has been installed.\nThe Template files may have changed.\n\nWould you like to install the new ones?"),
                i18n("Yes"), i18n("No") );
      if( !btn ) { // "Yes"
        bTemplates = false; // force reinstall
      }
    }
    return !bTemplates; // No templates installed, or update wanted
}

/*
 * Create, if necessary, some dirs and some .directory in user's .kde/
 * and copy Templates, still if necessary only
 */
void testLocalInstallation()
{
  // share and share/config already checked by KApplication
  UserPaths::testLocalDir( "/share/apps/kfm" ); // don't rename this to konqueror !
  UserPaths::testLocalDir( "/share/apps/kfm/bookmarks" ); // we want to keep user's bookmarks !
  UserPaths::testLocalDir( "/share/apps/konqueror" ); // for kfmclient
  UserPaths::testLocalDir( "/share/icons" );
  UserPaths::testLocalDir( "/share/icons/mini" );
  UserPaths::testLocalDir( "/share/applnk" );
  UserPaths::testLocalDir( "/share/mimelnk" );

  bool newRelease = isNewRelease();
  bool copyTemplates = checkTemplates(newRelease);

  testDir( UserPaths::desktopPath(), TRUE );
  copyDirectoryFile("directory.desktop", UserPaths::desktopPath(), false);
  testDir( UserPaths::trashPath() );
  copyDirectoryFile("directory.trash", UserPaths::trashPath(), newRelease); //for trash icon
  testDir( UserPaths::templatesPath() );
  copyDirectoryFile("directory.templates", UserPaths::templatesPath(), copyTemplates);
  testDir( UserPaths::autostartPath() );
  copyDirectoryFile("directory.autostart", UserPaths::autostartPath(), false);

  if (copyTemplates)
  {
    QString cmd;
    cmd.sprintf("cp %s/konqueror/Templates/* %s",
                kapp->kde_datadir().data(),
                UserPaths::templatesPath().data() );
    system( cmd.data() );
    KWM::sendKWMCommand("krootwm:refreshNew");
  }
}

/**********************************************
 *
 * MAIN
 *
 **********************************************/

int main( int argc, char **argv )
{
  // kfm uses lots of colors, especially when browsing the web
  // The call below helps for 256-color displays
  // Kudos to Nikita V. Youshchenko !
  QApplication::setColorSpec( QApplication::ManyColor ); 

  KonqApp app( argc, argv );
  
  // create a KdedInstance object, which takes care about connecting to kded
  // and providing a trader and an activator
  KdedInstance kded( argc, argv, komapp_orb );
  
  KonqBoot boot( "IDL:Konqueror/Application:1.0", "App" );
  
  KGlobal::locale()->insertCatalogue("libkonq"); // needed for apps using libkonq

  int i = 1;
  if ( strcmp( argv[i], "-s" ) == 0 || strcmp( argv[i], "--server" ) == 0 )
  {
    i++;
    g_bWithGUI = false;
  }

  signal(SIGCHLD,sig_handler);
  signal(SIGTERM,sig_term_handler);
  signal(SIGPIPE,sig_pipe_handler);

  KIOCache::initStatic();
  
  KonqFileManager fm;
  KTraderServiceProvider sp;

  testLocalInstallation();
  
  KRegistry registry;
  registry.addFactory( new KServiceTypeFactory );
  // registry.addFactory( new KServiceFactory ); // not needed anymore - we use kded
  registry.load( );

  kdebug(0, 1202, "===================== mime stuff finished ==============");

  kimgioRegister();

  QString path = kapp->localkdedir();
  path += "/share/apps/kfm/bookmarks";
  KonqBookmarkManager bm ( path );

  app.connect( &app, SIGNAL( lastWindowClosed() ), &app, SLOT( quit() ) ); 
  
  app.exec();

  kdebug(0, 1202, "============ BACK from event loop ===========");
  sig_term_handler(0);

  return 0;
}

void sig_handler( int )
{
    int pid;
    int status;

    while( 1 )
    {
	pid = waitpid( -1, &status, WNOHANG );
	if ( pid <= 0 )
	{
	    // Reinstall signal handler, since Linux resets to default after
	    // the signal occured ( BSD handles it different, but it should do
	    // no harm ).
	    signal( SIGCHLD, sig_handler );
	    return;
	}
    }
}

void sig_term_handler( int )
{
  kdebug( KDEBUG_INFO, 1202, "###################### SIG TERM ###################" );

  if ( KonqTopWidget::isGoingDown() )
    return;

  KonqTopWidget::getKonqTopWidget()->slotSave();
  KonqTopWidget::getKonqTopWidget()->slotShutDown();

  exit(1);
}

void sig_pipe_handler( int )
{
  kdebug( KDEBUG_INFO, 1202, "###################### SIG PIPE ###################" );
  signal( SIGPIPE, sig_pipe_handler );
}

#include "konq_main.moc"
