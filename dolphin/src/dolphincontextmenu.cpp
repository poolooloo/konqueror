/***************************************************************************
 *   Copyright (C) 2006 by Peter Penz (peter.penz@gmx.at) and              *
 *   Cvetoslav Ludmiloff                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#include "dolphincontextmenu.h"

#include "dolphinmainwindow.h"
#include "dolphinnewfilemenu.h"
#include "settings/dolphinsettings.h"
#include "dolphinviewcontainer.h"
#include "dolphin_generalsettings.h"

#include <kactioncollection.h>
#include <kdesktopfile.h>
#include <kfileitemlistproperties.h>
#include <kfileplacesmodel.h>
#include <kglobal.h>
#include <kiconloader.h>
#include <kio/netaccess.h>
#include <kmenu.h>
#include <kmenubar.h>
#include <kmessagebox.h>
#include <kmimetypetrader.h>
#include <knewfilemenu.h>
#include <konqmimedata.h>
#include <konq_operations.h>
#include <kfileitemactions.h>
#include <klocale.h>
#include <kpropertiesdialog.h>
#include <kstandardaction.h>
#include <kstandarddirs.h>

#include <QtGui/QApplication>
#include <QtGui/QClipboard>
#include <QtCore/QDir>

#include "views/dolphinview.h"
#include "views/viewmodecontroller.h"

DolphinContextMenu::DolphinContextMenu(DolphinMainWindow* parent,
                                       const KFileItem& fileInfo,
                                       const KUrl& baseUrl) :
    m_mainWindow(parent),
    m_capabilities(0),
    m_fileInfo(fileInfo),
    m_baseUrl(baseUrl),
    m_context(NoContext),
    m_copyToMenu(parent),
    m_customActions()
{
    // The context menu either accesses the URLs of the selected items
    // or the items itself. To increase the performance both lists are cached.
    const DolphinView* view = m_mainWindow->activeViewContainer()->view();
    m_selectedUrls = view->selectedUrls();
    m_selectedItems = view->selectedItems();
}

DolphinContextMenu::~DolphinContextMenu()
{
    delete m_capabilities;
    m_capabilities = 0;
}

void DolphinContextMenu::setCustomActions(const QList<QAction*>& actions)
{
    m_customActions = actions;
}

void DolphinContextMenu::open()
{
    // get the context information
    if (m_baseUrl.protocol() == "trash") {
        m_context |= TrashContext;
    }

    if (!m_fileInfo.isNull() && !m_selectedItems.isEmpty()) {
        m_context |= ItemContext;
        // TODO: handle other use cases like devices + desktop files
    }

    // open the corresponding popup for the context
    if (m_context & TrashContext) {
        if (m_context & ItemContext) {
            openTrashItemContextMenu();
        } else {
            openTrashContextMenu();
        }
    } else if (m_context & ItemContext) {
        openItemContextMenu();
    } else {
        Q_ASSERT(m_context == NoContext);
        openViewportContextMenu();
    }
}

void DolphinContextMenu::openTrashContextMenu()
{
    Q_ASSERT(m_context & TrashContext);

    KMenu* popup = new KMenu(m_mainWindow);

    addShowMenubarAction(popup);

    QAction* emptyTrashAction = new QAction(KIcon("trash-empty"), i18nc("@action:inmenu", "Empty Trash"), popup);
    KConfig trashConfig("trashrc", KConfig::SimpleConfig);
    emptyTrashAction->setEnabled(!trashConfig.group("Status").readEntry("Empty", true));
    popup->addAction(emptyTrashAction);

    QAction* addToPlacesAction = popup->addAction(KIcon("bookmark-new"),
                                                  i18nc("@action:inmenu Add current folder to places", "Add to Places"));

    // Don't show if url is already in places
    if (placeExists(m_mainWindow->activeViewContainer()->url())) {
        addToPlacesAction->setVisible(false);
    }

    addCustomActions(popup);

    QAction* propertiesAction = m_mainWindow->actionCollection()->action("properties");
    popup->addAction(propertiesAction);

    QAction *action = popup->exec(QCursor::pos());
    if (action == emptyTrashAction) {
        const QString text(i18nc("@info", "Do you really want to empty the Trash? All items will be deleted."));
        const bool del = KMessageBox::warningContinueCancel(m_mainWindow,
                                                            text,
                                                            QString(),
                                                            KGuiItem(i18nc("@action:button", "Empty Trash"),
                                                                     KIcon("user-trash"))
                                                           ) == KMessageBox::Continue;
        if (del) {
            KonqOperations::emptyTrash(m_mainWindow);
        }
    } else if (action == addToPlacesAction) {
        const KUrl& url = m_mainWindow->activeViewContainer()->url();
        if (url.isValid()) {
            DolphinSettings::instance().placesModel()->addPlace(i18nc("@label", "Trash"), url);
        }
    }

    popup->deleteLater();
}

void DolphinContextMenu::openTrashItemContextMenu()
{
    Q_ASSERT(m_context & TrashContext);
    Q_ASSERT(m_context & ItemContext);

    KMenu* popup = new KMenu(m_mainWindow);

    addShowMenubarAction(popup);

    QAction* restoreAction = new QAction(i18nc("@action:inmenu", "Restore"), m_mainWindow);
    popup->addAction(restoreAction);

    QAction* deleteAction = m_mainWindow->actionCollection()->action("delete");
    popup->addAction(deleteAction);

    QAction* propertiesAction = m_mainWindow->actionCollection()->action("properties");
    popup->addAction(propertiesAction);

    if (popup->exec(QCursor::pos()) == restoreAction) {
        KonqOperations::restoreTrashedItems(m_selectedUrls, m_mainWindow);
    }

    popup->deleteLater();
}

void DolphinContextMenu::openItemContextMenu()
{
    Q_ASSERT(!m_fileInfo.isNull());

    KMenu* popup = new KMenu(m_mainWindow);
    if (m_fileInfo.isDir() && (m_selectedUrls.count() == 1)) {
        // setup 'Create New' menu
        DolphinNewFileMenu* newFileMenu = new DolphinNewFileMenu(popup, m_mainWindow);
        const DolphinView* view = m_mainWindow->activeViewContainer()->view();
        newFileMenu->setViewShowsHiddenFiles(view->showHiddenFiles());
        newFileMenu->checkUpToDate();
        newFileMenu->setPopupFiles(m_fileInfo.url());
        newFileMenu->setEnabled(capabilities().supportsWriting());

        KMenu* menu = newFileMenu->menu();
        menu->setTitle(i18nc("@title:menu Create new folder, file, link, etc.", "Create New"));
        menu->setIcon(KIcon("document-new"));
        popup->addMenu(menu);
        popup->addSeparator();

        // insert 'Open in new window' and 'Open in new tab' entries
        popup->addAction(m_mainWindow->actionCollection()->action("open_in_new_window"));
        popup->addAction(m_mainWindow->actionCollection()->action("open_in_new_tab"));
        popup->addSeparator();
    }
    addShowMenubarAction(popup);
    insertDefaultItemActions(popup);

    popup->addSeparator();

    KFileItemActions fileItemActions;
    fileItemActions.setItemListProperties(capabilities());
    addServiceActions(popup, fileItemActions);

    addVersionControlActions(popup);

    // insert 'Copy To' and 'Move To' sub menus
    if (DolphinSettings::instance().generalSettings()->showCopyMoveMenu()) {
        m_copyToMenu.setItems(m_selectedItems);
        m_copyToMenu.setReadOnly(!capabilities().supportsWriting());
        m_copyToMenu.addActionsTo(popup);
        popup->addSeparator();
    }

    // insert 'Add to Places' entry if exactly one item is selected
    QAction* addToPlacesAction = 0;
    if (m_fileInfo.isDir() && (m_selectedUrls.count() == 1) && !placeExists(m_fileInfo.url())) {
        addToPlacesAction = popup->addAction(KIcon("bookmark-new"),
                                             i18nc("@action:inmenu Add selected folder to places", "Add to Places"));
    }

    // insert 'Properties...' entry
    QAction* propertiesAction = m_mainWindow->actionCollection()->action("properties");
    popup->addAction(propertiesAction);

    QAction* activatedAction = popup->exec(QCursor::pos());

    if ((addToPlacesAction != 0) && (activatedAction == addToPlacesAction)) {
        const KUrl selectedUrl(m_fileInfo.url());
        if (selectedUrl.isValid()) {
            DolphinSettings::instance().placesModel()->addPlace(placesName(selectedUrl),
                                                                selectedUrl);
        }
    }

    popup->deleteLater();
}

void DolphinContextMenu::openViewportContextMenu()
{
    KMenu* popup = new KMenu(m_mainWindow);

    addShowMenubarAction(popup);

    // setup 'Create New' menu
    KNewFileMenu* newFileMenu = m_mainWindow->newFileMenu();
    const DolphinView* view = m_mainWindow->activeViewContainer()->view();
    newFileMenu->setViewShowsHiddenFiles(view->showHiddenFiles());
    newFileMenu->checkUpToDate();
    newFileMenu->setPopupFiles(m_baseUrl);
    popup->addMenu(newFileMenu->menu());
    popup->addSeparator();

    // insert 'Open in new window' and 'Open in new tab' entries
    popup->addAction(m_mainWindow->actionCollection()->action("open_in_new_window"));
    popup->addAction(m_mainWindow->actionCollection()->action("open_in_new_tab"));
    popup->addSeparator();

    QAction* pasteAction = createPasteAction();
    popup->addAction(pasteAction);
    popup->addSeparator();

    // insert service actions
    const KFileItem item(KFileItem::Unknown, KFileItem::Unknown, m_baseUrl);
    const KFileItemListProperties baseUrlProperties(KFileItemList() << item);
    KFileItemActions fileItemActions;
    fileItemActions.setItemListProperties(baseUrlProperties);
    addServiceActions(popup, fileItemActions);

    addVersionControlActions(popup);

    // insert 'Add to Places' entry if exactly one item is selected
    QAction* addToPlacesAction = 0;
    if (!placeExists(m_mainWindow->activeViewContainer()->url())) {
        addToPlacesAction = popup->addAction(KIcon("bookmark-new"),
                                             i18nc("@action:inmenu Add current folder to places", "Add to Places"));
    }

    addCustomActions(popup);

    QAction* propertiesAction = popup->addAction(i18nc("@action:inmenu", "Properties"));
    propertiesAction->setIcon(KIcon("document-properties"));
    QAction* action = popup->exec(QCursor::pos());
    if (action == propertiesAction) {
        const KUrl& url = m_mainWindow->activeViewContainer()->url();

        KPropertiesDialog* dialog = new KPropertiesDialog(url, m_mainWindow);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    } else if ((addToPlacesAction != 0) && (action == addToPlacesAction)) {
        const KUrl& url = m_mainWindow->activeViewContainer()->url();
        if (url.isValid()) {
            DolphinSettings::instance().placesModel()->addPlace(placesName(url), url);
        }
    }

    popup->deleteLater();
}

void DolphinContextMenu::insertDefaultItemActions(KMenu* popup)
{
    Q_ASSERT(popup != 0);
    const KActionCollection* collection = m_mainWindow->actionCollection();

    // insert 'Cut', 'Copy' and 'Paste'
    QAction* cutAction = collection->action(KStandardAction::name(KStandardAction::Cut));
    QAction* copyAction  = collection->action(KStandardAction::name(KStandardAction::Copy));
    QAction* pasteAction = createPasteAction();

    popup->addAction(cutAction);
    popup->addAction(copyAction);
    popup->addAction(pasteAction);
    popup->addSeparator();

    // insert 'Rename'
    QAction* renameAction = collection->action("rename");
    popup->addAction(renameAction);

    // insert 'Move to Trash' and (optionally) 'Delete'
    KSharedConfig::Ptr globalConfig = KSharedConfig::openConfig("kdeglobals", KConfig::IncludeGlobals);
    KConfigGroup configGroup(globalConfig, "KDE");
    bool showDeleteCommand = configGroup.readEntry("ShowDeleteCommand", false);

    const KUrl& url = m_mainWindow->activeViewContainer()->url();
    if (url.isLocalFile()) {
        QAction* moveToTrashAction = collection->action("move_to_trash");
        popup->addAction(moveToTrashAction);
    } else {
        showDeleteCommand = true;
    }

    if (showDeleteCommand) {
        QAction* deleteAction = collection->action("delete");
        popup->addAction(deleteAction);
    }
}

void DolphinContextMenu::addShowMenubarAction(KMenu* menu)
{
    KAction* showMenuBar = m_mainWindow->showMenuBarAction();
    if (!m_mainWindow->menuBar()->isVisible()) {
        menu->addAction(showMenuBar);
        menu->addSeparator();
    }
}

QString DolphinContextMenu::placesName(const KUrl& url) const
{
    QString name = url.fileName();
    if (name.isEmpty()) {
        name = url.host();
    }
    return name;
}

bool DolphinContextMenu::placeExists(const KUrl& url) const
{
    const KFilePlacesModel* placesModel = DolphinSettings::instance().placesModel();
    const int count = placesModel->rowCount();

    for (int i = 0; i < count; ++i) {
        const QModelIndex index = placesModel->index(i, 0);

        if (url.equals(placesModel->url(index), KUrl::CompareWithoutTrailingSlash)) {
            return true;
        }
    }
    return false;
}

QAction* DolphinContextMenu::createPasteAction()
{
    QAction* action = 0;
    const bool isDir = !m_fileInfo.isNull() && m_fileInfo.isDir();
    if (isDir && (m_selectedItems.count() == 1)) {
        action = new QAction(KIcon("edit-paste"), i18nc("@action:inmenu", "Paste Into Folder"), this);
        const QMimeData* mimeData = QApplication::clipboard()->mimeData();
        const KUrl::List pasteData = KUrl::List::fromMimeData(mimeData);
        action->setEnabled(!pasteData.isEmpty() && capabilities().supportsWriting());
        connect(action, SIGNAL(triggered()), m_mainWindow, SLOT(pasteIntoFolder()));
    } else {
        action = m_mainWindow->actionCollection()->action(KStandardAction::name(KStandardAction::Paste));
    }

    return action;
}

KFileItemListProperties& DolphinContextMenu::capabilities()
{
    if (m_capabilities == 0) {
        m_capabilities = new KFileItemListProperties(m_selectedItems);
    }
    return *m_capabilities;
}

void DolphinContextMenu::addServiceActions(KMenu* menu, KFileItemActions& fileItemActions)
{
    fileItemActions.setParentWidget(m_mainWindow);

    // insert 'Open With...' action or sub menu
    fileItemActions.addOpenWithActionsTo(menu, "DesktopEntryName != 'dolphin'");

    // insert 'Actions' sub menu
    if (fileItemActions.addServiceActionsTo(menu)) {
        menu->addSeparator();
    }
}

void DolphinContextMenu::addVersionControlActions(KMenu* menu)
{
    const DolphinView* view = m_mainWindow->activeViewContainer()->view();
    const QList<QAction*> versionControlActions = view->versionControlActions(m_selectedItems);
    if (!versionControlActions.isEmpty()) {
        foreach (QAction* action, versionControlActions) {
            menu->addAction(action);
        }
        menu->addSeparator();
    }
}

void DolphinContextMenu::addCustomActions(KMenu* menu)
{
    foreach (QAction* action, m_customActions) {
        menu->addAction(action);
    }
}

#include "dolphincontextmenu.moc"
