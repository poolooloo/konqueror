//
//
// "Misc Options" Tab for KFM configuration
//
// (c) Sven Radej 1998
// (c) David Faure 1998

#ifndef HTMLOPTS_H
#define HTMLOPTS_H

#include <QtGui/QCheckBox>
#include <QtGui/QLineEdit>
#include <QtGui/QComboBox>

//-----------------------------------------------------------------------------
// The "Misc Options" Tab for the HTML view contains :

// Change cursor over links
// Underline links
// AutoLoad Images
// ... there is room for others :))



#include <kcmodule.h>
#include <ksharedconfig.h>
class KIntNumInput;

class KMiscHTMLOptions : public KCModule
{
    Q_OBJECT

public:
    KMiscHTMLOptions( QWidget *parent, const QVariantList& );
    ~KMiscHTMLOptions();
    virtual void load();
    virtual void save();
    virtual void defaults();

private Q_SLOTS:
    void slotChanged();

private:
    KSharedConfig::Ptr m_pConfig;
    QString  m_groupname;

    QComboBox* m_pUnderlineCombo;
    QComboBox* m_pAnimationsCombo;
    QCheckBox* m_cbCursor;
    QCheckBox* m_pAutoLoadImagesCheckBox;
    QCheckBox* m_pUnfinishedImageFrameCheckBox;
    QCheckBox* m_pAutoRedirectCheckBox;
    QCheckBox* m_pOpenMiddleClick;
    QCheckBox* m_pBackRightClick;
    QCheckBox* m_pFormCompletionCheckBox;
    QCheckBox* m_pAdvancedAddBookmarkCheckBox;
    QCheckBox* m_pOnlyMarkedBookmarksCheckBox;
    QCheckBox* m_pAccessKeys;
    KIntNumInput* m_pMaxFormCompletionItems;
};

#endif // HTMLOPTS_H
