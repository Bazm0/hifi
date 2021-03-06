//
//  MenuScriptingInterface.h
//  interface/src/scripting
//
//  Created by Brad Hefta-Gaub on 2/25/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MenuScriptingInterface_h
#define hifi_MenuScriptingInterface_h

#include <QDebug>
#include <QMutex>
#include <QObject>
#include <QString>

#include "Menu.h"
#include <MenuItemProperties.h>

class MenuScriptingInterface : public QObject {
    Q_OBJECT
    MenuScriptingInterface() { };
public:
    static MenuScriptingInterface* getInstance();

private slots:
    friend class Menu;
    void menuItemTriggered();

public slots:
    void addMenu(const QString& menuName);
    void removeMenu(const QString& menuName);

    void addSeparator(const QString& menuName, const QString& separatorName);
    void removeSeparator(const QString& menuName, const QString& separatorName);
    
    void addMenuItem(const MenuItemProperties& properties);
    void addMenuItem(const QString& menuName, const QString& menuitem, const QString& shortcutKey);
    void addMenuItem(const QString& menuName, const QString& menuitem);

    void removeMenuItem(const QString& menuName, const QString& menuitem);

    bool isOptionChecked(const QString& menuOption);
    void setIsOptionChecked(const QString& menuOption, bool isChecked);
    
signals:
    void menuItemEvent(const QString& menuItem);
};

#endif // hifi_MenuScriptingInterface_h
