/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Was Utils.mjs. A module that lists a JavaScript file in its qmldir must not be
// imported twice into one namespace, and Qt 6.8 does not merge the transitive
// imports of a module with a file's own import of the same module, so every
// "import Muse.UiComponents" next to a module that imports it as well failed with
// '"Muse.UiComponents" is ambiguous'. As a singleton the helpers are reached the
// same way (Utils.colorWithAlpha(...)) and the module carries no script.
pragma Singleton

import QtQuick

QtObject {
    id: root

    function colorWithAlpha(color, alpha) {
        if (typeof color === "string") {
            // Hack: convert to color
            color = Qt.lighter(color, 1.0)
        }

        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    function accessibleColorDescription(color) {
        var percentName = qsTrc("global", "percent")
        var colorValueToPercent = function(value) {
            return Math.floor(value * 100)
        }

        var colorValueTemplate = "%1 %2 %3"
        var text = colorValueTemplate.arg(qsTrc("ui", "Red")).arg(colorValueToPercent(color.r)).arg(percentName)
                + " " + colorValueTemplate.arg(qsTrc("ui", "Green")).arg(colorValueToPercent(color.g)).arg(percentName)
                + " " + colorValueTemplate.arg(qsTrc("ui", "Blue")).arg(colorValueToPercent(color.b)).arg(percentName)

        return text
    }

    function ensureContentVisible(flickable, contentRect, margins) {
        var flickableBottomY = flickable.contentY + flickable.height
        var contentBottomY = contentRect.y + contentRect.height

        var flickableTopY = flickable.contentY
        var contentTopY = contentRect.y

        if (flickableBottomY < contentBottomY) {
            flickable.contentY += contentBottomY - flickableBottomY + margins
        } else if (flickableTopY > contentTopY) {
            flickable.contentY -= flickableTopY - contentTopY + margins
        }
    }

    function getItem(model, index) {
        if (!Boolean(model)) {
            return null
        }

        if (Boolean(model.get)) {
            return model.get(index)
        }

        return model[index]
    }

    function getItemValue(model, index, roleName, def) {
        var item = root.getItem(model, index)
        if (!Boolean(item)) {
            return def
        }

        if (item[roleName] !== undefined) {
            return item[roleName]
        }

        return item
    }

    function areEqual(a, b) {
        if (a === b) {
            return true
        }
        if (typeof a === 'object' && typeof b === 'object') {
            return JSON.stringify(a) === JSON.stringify(b)
        }
        return false
    }
}
