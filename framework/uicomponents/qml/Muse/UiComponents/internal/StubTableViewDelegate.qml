/*
* SPDX-License-Identifier: GPL-3.0-only
* MuseScore-CLA-applies
*
* MuseScore Studio
* Music Composition & Notation
*
* Copyright (C) 2025 MuseScore Limited and others
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 3 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <https://www.gnu.org/licenses/>.
*/
import QtQuick
import QtQuick.Controls

// Stand-in for QtQuick.Controls.TableViewDelegate (Qt >= 6.9) on older Qt, built
// only there and registered under that name. It provides what StyledTableViewCell
// uses: row, column, current, selected, editing and tableView; TableView of Qt 6.8
// fills the required properties by name and offers the view as an attached property.
ItemDelegate {
    required property int row
    required property int column
    required property bool current
    required property bool selected
    required property bool editing

    readonly property TableView tableView: TableView.view

    highlighted: selected || current
}
