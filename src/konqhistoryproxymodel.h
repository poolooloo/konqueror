/* This file is part of the KDE project
   Copyright 2009 Pino Toscano <pino@kde.org>

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
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef KONQ_HISTORYPROXYMODEL_H
#define KONQ_HISTORYPROXYMODEL_H

#include "ksortfilterproxymodel.h"

class KonqHistorySettings;

/**
 * Proxy model used for sorting and filtering the history model.
 *
 * It uses KSortFilterProxyModel instead of QSortFilterProxyModel so that one can
 * search in the history by typing parts of a page title (KSortFilterProxyModel keeps
 * parents of filtered items).
 */
class KonqHistoryProxyModel : public KSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit KonqHistoryProxyModel(KonqHistorySettings *settings, QObject *parent = Q_NULLPTR);
    ~KonqHistoryProxyModel();

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const Q_DECL_OVERRIDE;

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const Q_DECL_OVERRIDE;

private Q_SLOTS:
    void slotSettingsChanged();

private:
    KonqHistorySettings *m_settings;
};

#endif // KONQ_HISTORYPROXYMODEL_H
