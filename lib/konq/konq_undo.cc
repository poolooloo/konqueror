/* This file is part of the KDE project
   Copyright (C) 2000 Simon Hausmann <hausmann@kde.org>
   Copyright (C) 2006 David Faure <faure@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "konq_undo.h"
#include "konq_undo_p.h"
#include "undomanageradaptor.h"
#include "kio/observer.h"
#include <kio/jobuidelegate.h>
#include <QtDBus/QtDBus>
#include <kdirnotify.h>

#include <kdebug.h>
#include <klocale.h>
#include <kglobalsettings.h>
#include <kconfig.h>

#include <kio/job.h>

#include <assert.h>


/**
 * checklist:
 * copy dir -> overwrite -> works
 * move dir -> overwrite -> works
 * copy dir -> rename -> works
 * move dir -> rename -> works
 *
 * copy dir -> works
 * move dir -> works
 *
 * copy files -> works
 * move files -> works (TODO: optimize (change FileCopyJob to use the renamed arg for copyingDone)
 *
 * copy files -> overwrite -> works (sorry for your overwritten file...)
 * move files -> overwrite -> works (sorry for your overwritten file...)
 *
 * copy files -> rename -> works
 * move files -> rename -> works
 *
 * -> see also konqundomanagertest, which aims at testing all the above.
 *
 * TODO: fix http://bugs.kde.org/show_bug.cgi?id=20532
 *    (Undoing a copy operation might delete a modified file causing loss of data)
 *   by storing+comparing modification time.
 *
 */

class KonqUndoJob : public KIO::Job
{
public:
    KonqUndoJob() : KIO::Job( true ) { KonqUndoManager::incRef(); };
    virtual ~KonqUndoJob() { KonqUndoManager::decRef(); }

    virtual void kill( bool ) { KonqUndoManager::self()->stopUndo( true ); KIO::Job::doKill(); }
};

KonqCommandRecorder::KonqCommandRecorder( KonqUndoManager::CommandType op, const KUrl::List &src, const KUrl &dst, KIO::Job *job )
  : QObject( job )
{
  setObjectName( "konqcmdrecorder" );

  m_cmd.m_type = op;
  m_cmd.m_valid = true;
  m_cmd.m_src = src;
  m_cmd.m_dst = dst;
  connect( job, SIGNAL( result( KJob * ) ),
           this, SLOT( slotResult( KJob * ) ) );

  if ( op != KonqUndoManager::MKDIR ) {
      connect( job, SIGNAL( copyingDone( KIO::Job *, const KUrl &, const KUrl &, bool, bool ) ),
               this, SLOT( slotCopyingDone( KIO::Job *, const KUrl &, const KUrl &, bool, bool ) ) );
      connect( job, SIGNAL( copyingLinkDone( KIO::Job *, const KUrl &, const QString &, const KUrl & ) ),
               this, SLOT( slotCopyingLinkDone( KIO::Job *, const KUrl &, const QString &, const KUrl & ) ) );
  }

  KonqUndoManager::incRef();
}

KonqCommandRecorder::~KonqCommandRecorder()
{
  KonqUndoManager::decRef();
}

void KonqCommandRecorder::slotResult( KJob *job )
{
  if ( job->error() )
    return;

  KonqUndoManager::self()->addCommand( m_cmd );
}

void KonqCommandRecorder::slotCopyingDone( KIO::Job *job, const KUrl &from, const KUrl &to, bool directory, bool renamed )
{
  KonqBasicOperation op;
  op.m_valid = true;
  op.m_directory = directory;
  op.m_renamed = renamed;
  op.m_src = from;
  op.m_dst = to;
  op.m_link = false;

  if ( m_cmd.m_type == KonqUndoManager::TRASH )
  {
      Q_ASSERT( from.isLocalFile() );
      Q_ASSERT( to.protocol() == "trash" );
      QMap<QString, QString> metaData = job->metaData();
      QMap<QString, QString>::ConstIterator it = metaData.find( "trashURL-" + from.path() );
      if ( it != metaData.end() ) {
          // Update URL
          op.m_dst = it.value();
      }
  }

  m_cmd.m_opStack.prepend( op );
}

void KonqCommandRecorder::slotCopyingLinkDone( KIO::Job *, const KUrl &from, const QString &target, const KUrl &to )
{
  KonqBasicOperation op;
  op.m_valid = true;
  op.m_directory = false;
  op.m_renamed = false;
  op.m_src = from;
  op.m_target = target;
  op.m_dst = to;
  op.m_link = true;
  m_cmd.m_opStack.prepend( op );
}

KonqUndoManager *KonqUndoManager::s_self = 0;
unsigned long KonqUndoManager::s_refCnt = 0;

class KonqUndoManager::KonqUndoManagerPrivate
{
public:
  KonqUndoManagerPrivate()
  {
      m_undoJob = 0;
  }
  ~KonqUndoManagerPrivate()
  {
  }

  bool m_syncronized;
  bool m_lock;

  KonqCommand::Stack m_commands;

  KonqCommand m_current;
  KIO::Job *m_currentJob;
  UndoState m_undoState;
  QStack<KUrl> m_dirStack;
  QStack<KUrl> m_dirCleanupStack;
  QStack<KUrl> m_fileCleanupStack;
  QList<KUrl> m_dirsToUpdate;

  KonqUndoJob *m_undoJob;
};

KonqUndoManager::KonqUndoManager()
{
  (void) new UndoManagerAdaptor( this );
  const QString dbusPath = "/KonqUndoManager";
  const QString dbusInterface = "org.kde.libkonq.UndoManager";

  QDBusConnection dbus = QDBusConnection::sessionBus();
  dbus.registerObject( dbusPath, this );
  dbus.connect(QString(), dbusPath, dbusInterface, "lock", this, SLOT(slotLock()));
  dbus.connect(QString(), dbusPath, dbusInterface, "pop", this, SLOT(slotPop()));
  dbus.connect(QString(), dbusPath, dbusInterface, "push", this, SLOT(slotPush(QByteArray)) );
  dbus.connect(QString(), dbusPath, dbusInterface, "unlock", this, SLOT(slotUnlock()) );

  d = new KonqUndoManagerPrivate;
  d->m_syncronized = initializeFromKDesky();
  d->m_lock = false;
  d->m_currentJob = 0;
}

KonqUndoManager::~KonqUndoManager()
{
  delete d;
}

void KonqUndoManager::incRef()
{
  s_refCnt++;
}

void KonqUndoManager::decRef()
{
  s_refCnt--;
  if ( s_refCnt == 0 && s_self )
  {
    delete s_self;
    s_self = 0;
  }
}

KonqUndoManager *KonqUndoManager::self()
{
  if ( !s_self )
  {
    if ( s_refCnt == 0 )
      s_refCnt++; // someone forgot to call incRef
    s_self = new KonqUndoManager;
  }
  return s_self;
}

void KonqUndoManager::recordJob( CommandType op, const KUrl::List &src, const KUrl &dst, KIO::Job *job )
{
    // This records what the job does and calls addCommand when done
    (void) new KonqCommandRecorder( op, src, dst, job );
}

void KonqUndoManager::addCommand( const KonqCommand &cmd )
{
    broadcastPush( cmd );
}

bool KonqUndoManager::undoAvailable() const
{
  return ( d->m_commands.count() > 0 ) && !d->m_lock;
}

QString KonqUndoManager::undoText() const
{
  if ( d->m_commands.isEmpty() )
    return i18n( "Und&o" );

  KonqUndoManager::CommandType t = d->m_commands.top().m_type;
  if ( t == KonqUndoManager::COPY )
    return i18n( "Und&o: Copy" );
  else if ( t == KonqUndoManager::LINK )
    return i18n( "Und&o: Link" );
  else if ( t == KonqUndoManager::MOVE )
    return i18n( "Und&o: Move" );
  // distinguish renaming from moving - #61442
  else if ( t == KonqUndoManager::RENAME )
    return i18n( "Und&o: Rename" );
  else if ( t == KonqUndoManager::TRASH )
    return i18n( "Und&o: Trash" );
  else if ( t == KonqUndoManager::MKDIR )
    return i18n( "Und&o: Create Folder" );
  else
    assert( false );
  /* NOTREACHED */
  return QString();
}

void KonqUndoManager::undo()
{
  KonqCommand cmd = d->m_commands.top();
  broadcastPop();
  broadcastLock();

  assert( cmd.m_valid );

  d->m_current = cmd;
  d->m_dirCleanupStack.clear();
  d->m_dirStack.clear();
  d->m_dirsToUpdate.clear();

  d->m_undoState = MOVINGFILES;
  kDebug(1203) << "KonqUndoManager::undo MOVINGFILES" << endl;

  KonqBasicOperation::Stack& opStack = d->m_current.m_opStack;
  assert( !opStack.isEmpty() );

  QStack<KonqBasicOperation>::Iterator it = opStack.begin();
  while ( it != opStack.end() )
  {
    bool removeBasicOperation = false;
    if ( (*it).m_directory && !(*it).m_renamed )
    {
      d->m_dirStack.push( (*it).m_src );
      d->m_dirCleanupStack.prepend( (*it).m_dst );
      removeBasicOperation = true;
      d->m_undoState = MAKINGDIRS;
      kDebug(1203) << "KonqUndoManager::undo MAKINGDIRS" << endl;
    }
    else if ( (*it).m_link )
    {
      if ( !d->m_fileCleanupStack.contains( (*it).m_dst ) )
        d->m_fileCleanupStack.prepend( (*it).m_dst );

      removeBasicOperation = !d->m_current.isMoveCommand();
    }

    if ( removeBasicOperation )
      it = opStack.erase( it );
    else
      ++it;
  }

  /* this shouldn't be necessary at all:
   * 1) the source list may contain files, we don't want to
   *    create those as... directories
   * 2) all directories that need creation should already be in the
   *    directory stack
  if ( d->m_undoState == MAKINGDIRS )
  {
    KUrl::List::ConstIterator it = d->m_current.m_src.begin();
    KUrl::List::ConstIterator end = d->m_current.m_src.end();
    for (; it != end; ++it )
      if ( !d->m_dirStack.contains( *it) )
        d->m_dirStack.push( *it );
  }
  */

  if ( !d->m_current.isMoveCommand() )
    d->m_dirStack.clear();

  d->m_undoJob = new KonqUndoJob;
  undoStep();
}

void KonqUndoManager::stopUndo( bool step )
{
    d->m_current.m_opStack.clear();
    d->m_dirCleanupStack.clear();
    d->m_fileCleanupStack.clear();
    d->m_undoState = REMOVINGDIRS;
    d->m_undoJob = 0;

    if ( d->m_currentJob )
        d->m_currentJob->doKill();

    d->m_currentJob = 0;

    if ( step )
        undoStep();
}

void KonqUndoManager::slotResult( KJob *job )
{
  Observer::self()->jobFinished( d->m_undoJob->progressId() );
  if ( job->error() )
  {
    static_cast<KIO::Job*>( job )->ui()->setWindow(0L);
    static_cast<KIO::Job*>( job )->ui()->showErrorMessage();
    d->m_currentJob = 0;
    stopUndo( false );
    delete d->m_undoJob;
    d->m_undoJob = 0;
  }

  undoStep();
}


void KonqUndoManager::addDirToUpdate( const KUrl& url )
{
  if ( !d->m_dirsToUpdate.contains( url ) )
    d->m_dirsToUpdate.prepend( url );
}

void KonqUndoManager::undoStep()
{
  d->m_currentJob = 0;

  if ( d->m_undoState == MAKINGDIRS )
      undoMakingDirectories();

  if ( d->m_undoState == MOVINGFILES )
      undoMovingFiles();

  if ( d->m_undoState == REMOVINGFILES )
      undoRemovingFiles();

  if ( d->m_undoState == REMOVINGDIRS )
      undoRemovingDirectories();

  if ( d->m_currentJob )
    connect( d->m_currentJob, SIGNAL( result( KJob * ) ),
             this, SLOT( slotResult( KJob * ) ) );
}

void KonqUndoManager::undoMakingDirectories()
{
    if ( !d->m_dirStack.isEmpty() ) {
      KUrl dir = d->m_dirStack.pop();
      kDebug(1203) << "KonqUndoManager::undoStep creatingDir " << dir.prettyUrl() << endl;
      d->m_currentJob = KIO::mkdir( dir );
      Observer::self()->slotCreatingDir( d->m_undoJob, dir );
    }
    else
      d->m_undoState = MOVINGFILES;
}

void KonqUndoManager::undoMovingFiles()
{
    if ( !d->m_current.m_opStack.isEmpty() )
    {
      KonqBasicOperation op = d->m_current.m_opStack.pop();

      assert( op.m_valid );
      if ( op.m_directory )
      {
        if ( op.m_renamed )
        {
          kDebug(1203) << "KonqUndoManager::undoStep rename " << op.m_dst.prettyUrl() << " " << op.m_src.prettyUrl() << endl;
          d->m_currentJob = KIO::rename( op.m_dst, op.m_src, false );
          Observer::self()->slotMoving( d->m_undoJob, op.m_dst, op.m_src );
        }
        else
          assert( 0 ); // this should not happen!
      }
      else if ( op.m_link )
      {
        kDebug(1203) << "KonqUndoManager::undoStep symlink " << op.m_target << " " << op.m_src.prettyUrl() << endl;
        d->m_currentJob = KIO::symlink( op.m_target, op.m_src, true, false );
      }
      else if ( d->m_current.m_type == KonqUndoManager::COPY )
      {
        kDebug(1203) << "KonqUndoManager::undoStep file_delete " << op.m_dst.prettyUrl() << endl;
        d->m_currentJob = KIO::file_delete( op.m_dst );
        Observer::self()->slotDeleting( d->m_undoJob, op.m_dst );
      }
      else if ( d->m_current.isMoveCommand()
                || d->m_current.m_type == KonqUndoManager::TRASH )
      {
        kDebug(1203) << "KonqUndoManager::undoStep file_move " << op.m_dst.prettyUrl() << " " << op.m_src.prettyUrl() << endl;
        d->m_currentJob = KIO::file_move( op.m_dst, op.m_src, -1, true );
        Observer::self()->slotMoving( d->m_undoJob, op.m_dst, op.m_src );
      }

      // The above KIO jobs are lowlevel, they don't trigger KDirNotify notification
      // So we need to do it ourselves (but schedule it to the end of the undo, to compress them)
      KUrl url( op.m_dst );
      url.setPath( url.directory() );
      addDirToUpdate( url );

      url = op.m_src;
      url.setPath( url.directory() );
      addDirToUpdate( url );
    }
    else
      d->m_undoState = REMOVINGFILES;
}

void KonqUndoManager::undoRemovingFiles()
{
    kDebug(1203) << "KonqUndoManager::undoStep REMOVINGFILES" << endl;
    if ( !d->m_fileCleanupStack.isEmpty() )
    {
      KUrl file = d->m_fileCleanupStack.pop();
      kDebug(1203) << "KonqUndoManager::undoStep file_delete " << file.prettyUrl() << endl;
      d->m_currentJob = KIO::file_delete( file );
      Observer::self()->slotDeleting( d->m_undoJob, file );

      KUrl url( file );
      url.setPath( url.directory() );
      addDirToUpdate( url );
    }
    else
    {
      d->m_undoState = REMOVINGDIRS;

      if ( d->m_dirCleanupStack.isEmpty() && d->m_current.m_type == KonqUndoManager::MKDIR )
        d->m_dirCleanupStack << d->m_current.m_dst;
    }
}

void KonqUndoManager::undoRemovingDirectories()
{
    if ( !d->m_dirCleanupStack.isEmpty() )
    {
      KUrl dir = d->m_dirCleanupStack.pop();
      kDebug(1203) << "KonqUndoManager::undoStep rmdir " << dir.prettyUrl() << endl;
      d->m_currentJob = KIO::rmdir( dir );
      Observer::self()->slotDeleting( d->m_undoJob, dir );
      addDirToUpdate( dir );
    }
    else
    {
      d->m_current.m_valid = false;
      d->m_currentJob = 0;
      if ( d->m_undoJob )
      {
          kDebug(1203) << "KonqUndoManager::undoStep deleting undojob" << endl;
          Observer::self()->jobFinished( d->m_undoJob->progressId() );
          delete d->m_undoJob;
          d->m_undoJob = 0;
      }
      QList<KUrl>::ConstIterator it = d->m_dirsToUpdate.begin();
      for( ; it != d->m_dirsToUpdate.end(); ++it ) {
          kDebug() << "Notifying FilesAdded for " << *it << endl;
		  org::kde::KDirNotify::emitFilesAdded( (*it).url() );
      }
      emit undoJobFinished();
      broadcastUnlock();
    }
}

void KonqUndoManager::slotPush( QByteArray data )
{
  QDataStream strm( &data, QIODevice::ReadOnly );
  KonqCommand cmd;
  strm >> cmd;
  pushCommand( cmd );
}

void KonqUndoManager::pushCommand( const KonqCommand& cmd )
{
  d->m_commands.push( cmd );
  emit undoAvailable( true );
  emit undoTextChanged( undoText() );
}

void KonqUndoManager::slotPop()
{
  d->m_commands.pop();
  emit undoAvailable( undoAvailable() );
  emit undoTextChanged( undoText() );
}

void KonqUndoManager::slotLock()
{
//  assert( !d->m_lock );
  d->m_lock = true;
  emit undoAvailable( undoAvailable() );
}

void KonqUndoManager::slotUnlock()
{
//  assert( d->m_lock );
  d->m_lock = false;
  emit undoAvailable( undoAvailable() );
}

QByteArray KonqUndoManager::get() const
{
  QByteArray data;
  QDataStream stream( &data, QIODevice::WriteOnly );
  stream << d->m_commands;
  return data;
}

void KonqUndoManager::broadcastPush( const KonqCommand &cmd )
{
  if ( !d->m_syncronized )
  {
    pushCommand( cmd );
    return;
  }

  QByteArray data;
  QDataStream stream( &data, QIODevice::WriteOnly );
  stream << cmd;
  emit push( data ); // DBUS signal
}

void KonqUndoManager::broadcastPop()
{
  if ( !d->m_syncronized )
  {
    slotPop();
    return;
  }

  emit pop(); // DBUS signal
}

void KonqUndoManager::broadcastLock()
{
//  assert( !d->m_lock );

  if ( !d->m_syncronized )
  {
    slotLock();
    return;
  }
  emit lock(); // DBUS signal
}

void KonqUndoManager::broadcastUnlock()
{
//  assert( d->m_lock );

  if ( !d->m_syncronized )
  {
    slotUnlock();
    return;
  }
  emit unlock(); // DBUS signal
}

bool KonqUndoManager::initializeFromKDesky()
{
  // ### workaround for dcop problem and upcoming 2.1 release:
  // in case of huge io operations the amount of data sent over
  // dcop (containing undo information broadcasted for global undo
  // to all konqueror instances) can easily exceed the 64kb limit
  // of dcop. In order not to run into trouble we disable global
  // undo for now! (Simon)
  // ### FIXME: post 2.1
  // TODO KDE4: port to DBUS and test
  return false;
#if 0
  DCOPClient *client = kapp->dcopClient();

  if ( client->appId() == "kdesktop" ) // we are master :)
    return true;

  if ( !client->isApplicationRegistered( "kdesktop" ) )
    return false;

  d->m_commands = DCOPRef( "kdesktop", "KonqUndoManager" ).call( "get" );
  return true;
#endif
}

QDataStream &operator<<( QDataStream &stream, const KonqBasicOperation &op )
{
    stream << op.m_valid << op.m_directory << op.m_renamed << op.m_link
           << op.m_src << op.m_dst << op.m_target;
  return stream;
}
QDataStream &operator>>( QDataStream &stream, KonqBasicOperation &op )
{
  stream >> op.m_valid >> op.m_directory >> op.m_renamed >> op.m_link
         >> op.m_src >> op.m_dst >> op.m_target;
  return stream;
}

QDataStream &operator<<( QDataStream &stream, const KonqCommand &cmd )
{
  stream << cmd.m_valid << (qint8)cmd.m_type << cmd.m_opStack << cmd.m_src << cmd.m_dst;
  return stream;
}

QDataStream &operator>>( QDataStream &stream, KonqCommand &cmd )
{
  qint8 type;
  stream >> cmd.m_valid >> type >> cmd.m_opStack >> cmd.m_src >> cmd.m_dst;
  cmd.m_type = static_cast<KonqUndoManager::CommandType>( type );
  return stream;
}

#include "konq_undo.moc"
#include "konq_undo_p.moc"
