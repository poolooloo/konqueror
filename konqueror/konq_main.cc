/* This file is part of the KDE project
   Copyright (C) 1998, 1999 Simon Hausmann <hausmann@kde.org>

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

#include <iostream.h>

#include "konq_main.h"
#include "konq_misc.h"
#include "konq_factory.h"
#include "konq_mainwindow.h"
#include "KonquerorIface.h"

#include <klocale.h>
#include <kglobal.h>
#include <kstddirs.h>
#include <kapp.h>
#include <kdebug.h>
#include <kcmdlineargs.h>
#include <kaboutdata.h>
#include <dcopclient.h>
#include <kimageio.h>
#include <kopenwith.h>

static KCmdLineOptions options[] =
{
  { "silent", I18N_NOOP("Start without a default window."), 0 },
  { "profile <profile>",   I18N_NOOP("Profile to open."), 0 },
  { "+[URL]",   I18N_NOOP("Location to open."), 0 },
  { 0, 0, 0}
};

int main( int argc, char **argv )
{
  KCmdLineArgs::init( argc, argv, KonqFactory::aboutData() );

  KCmdLineArgs::addCmdLineOptions( options ); // Add our own options.

  KonquerorApplication app;

  app.dcopClient()->registerAs( "konqueror" );

  app.dcopClient()->setDefaultObject( (new KonquerorIface())->objId() );

  KGlobal::locale()->insertCatalogue("libkonq"); // needed for apps using libkonq
  KImageIO::registerFormats();

  QString path = KGlobal::dirs()->saveLocation("data", "kfm/bookmarks", true);
  KonqBookmarkManager bm ( path );
  KFileOpenWithHandler fowh;

  KCmdLineArgs *args = KCmdLineArgs::parsedArgs();

  if ( kapp->isRestored() )
  {
    int n = 1;
    while ( KonqMainWindow::canBeRestored( n ) )
    {
      (new KonqMainWindow( KURL(), false ) )->restore( n );
      n++;
    }
  }
  else
  {
     if (args->isSet("profile"))
     {
       QString profile = QString::fromLocal8Bit(args->getOption("profile"));
       QString profilePath = profile;
       if (profile[0] != '/')
           profilePath = locate( "data", QString::fromLatin1("konqueror/profiles/")+profile );
       QString url;
       if (args->count() == 1)
           url = QString::fromLocal8Bit(args->arg(0));
       KonqMisc::createBrowserWindowFromProfile( profilePath, profile, url );
     }
     else
     {
         if (args->count() == 0)
         {
             if (!args->isSet("silent"))
             {
                 KonqMainWindow *mainWindow = new KonqMainWindow;
                 mainWindow->show();
             }
         }
         else
         {
             for ( int i = 0; i < args->count(); i++ )
             {
                 // KonqMisc::konqFilteredURL doesn't cope with local files... A bit of hackery below
                 KURL url = args->url(i);
                 KURL urlToOpen;
                 if (url.isLocalFile() && QFile::exists(url.path())) // "konqueror index.html"
                     urlToOpen = url;
                 else
                     urlToOpen = KURL( KonqMisc::konqFilteredURL(0L, args->arg(i)) ); // "konqueror slashdot.org"
                 //KonqMisc::createSimpleWindow( urlToOpen );
                 KonqMisc::createNewWindow( urlToOpen );
             }
         }
     }
  }
  args->clear();

  app.exec();

  return 0;
}
