/**************************************************************************
 *                                                                        *
 * Copyright (C) 2015 Felix Rohrbach <kde@fxrh.de>                        *
 *                                                                        *
 * This program is free software; you can redistribute it and/or          *
 * modify it under the terms of the GNU General Public License            *
 * as published by the Free Software Foundation; either version 3         *
 * of the License, or (at your option) any later version.                 *
 *                                                                        *
 * This program is distributed in the hope that it will be useful,        *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 * GNU General Public License for more details.                           *
 *                                                                        *
 * You should have received a copy of the GNU General Public License      *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
 *                                                                        *
 **************************************************************************/

#include "mainwindow.h"

#include <QtCore/QTimer>
#include <QtCore/QDebug>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>
#include <QtWidgets/QAction>
#include <QtWidgets/QInputDialog>

#include "quaternionconnection.h"
#include "roomlistdock.h"
#include "userlistdock.h"
#include "chatroomwidget.h"
#include "logindialog.h"
#include "systemtray.h"
#include "lib/jobs/geteventsjob.h"

MainWindow::MainWindow()
{
    setWindowIcon(QIcon(":/icon.png"));
    connection = nullptr;
    roomListDock = new RoomListDock(this);
    addDockWidget(Qt::LeftDockWidgetArea, roomListDock);
    userListDock = new UserListDock(this);
    addDockWidget(Qt::RightDockWidgetArea, userListDock);
    chatRoomWidget = new ChatRoomWidget(this);
    setCentralWidget(chatRoomWidget);
    connect( roomListDock, &RoomListDock::roomSelected, chatRoomWidget, &ChatRoomWidget::setRoom );
    connect( roomListDock, &RoomListDock::roomSelected, userListDock, &UserListDock::setRoom );
    systemTray = new SystemTray(this);
    systemTray->show();
    show();
    QTimer::singleShot(0, this, SLOT(initialize()));
}

MainWindow::~MainWindow()
{
}

void MainWindow::enableDebug()
{
    chatRoomWidget->enableDebug();
}

void MainWindow::initialize()
{
    auto menuBar = new QMenuBar();
    { // Connection menu
        auto connectionMenu = menuBar->addMenu(tr("&Connection"));

        loginAction = connectionMenu->addAction(tr("&Login..."));
        connect( loginAction, &QAction::triggered, this, &MainWindow::showLoginWindow);

        logoutAction = connectionMenu->addAction(tr("&Logout"));
        connect( logoutAction, &QAction::triggered, this, &MainWindow::logout);

        connectionMenu->addSeparator();

        auto quitAction = connectionMenu->addAction(tr("&Quit"));
        quitAction->setShortcut(QKeySequence(QKeySequence::Quit));
        connect( quitAction, &QAction::triggered, qApp, &QApplication::quit );
    }
    { // Room menu
        auto roomMenu = menuBar->addMenu(tr("&Room"));

        auto joinRoomAction = roomMenu->addAction(tr("&Join Room..."));
        connect( joinRoomAction, &QAction::triggered, this, &MainWindow::showJoinRoomDialog );
    }

    setMenuBar(menuBar);

    {
        QSettings settings("Quaternion");
        settings.beginGroup("Login");
        if (settings.value("savecredentials", false).toBool() &&
                settings.contains("userid") && settings.contains("token"))
        {
            QUrl url = QUrl::fromUserInput(settings.value("homeserver").toString());
            setConnection(new QuaternionConnection(url));
    
            connection->connectWithToken(settings.value("userid").toString(),
                                         settings.value("token").toString());
            loginAction->setEnabled(false);
        } else {
            showLoginWindow();
        }
        settings.endGroup();
    }

}

void MainWindow::setConnection(QuaternionConnection* newConnection)
{
    using QMatrixClient::Connection;
    if (connection)
    {
        connection->disconnect( this ); // Disconnects all signals, not the connection itself
        connection->deleteLater();
    }

    connection = newConnection;
    chatRoomWidget->setConnection(connection);
    userListDock->setConnection(connection);
    roomListDock->setConnection(connection);
    systemTray->setConnection(connection);

    if (connection)
    {
        connect( connection, &Connection::connectionError, this, &MainWindow::connectionError );
        connect( connection, &Connection::syncDone, this, &MainWindow::gotEvents );
        connect( connection, &Connection::reconnected, this, &MainWindow::getNewEvents );
        connect( connection, &Connection::loginError, this, &MainWindow::loggedOut );
        connect( connection, &Connection::loggedOut, this, &MainWindow::loggedOut );

        connection->sync(); // Enter an eternal chain of syncs
    }
}

void MainWindow::showLoginWindow()
{
    LoginDialog dialog(this);
    if( dialog.exec() )
    {
        QSettings settings("Quaternion");
        settings.beginGroup("Login");
        if (settings.value("savecredentials", false).toBool())
        {
            settings.setValue("userid", connection->userId());
            settings.setValue("token", connection->token());
        } else {
            settings.remove("userid");
            settings.remove("token");
        }
        settings.endGroup();

        logoutAction->setEnabled(true);
        loginAction->setEnabled(false);
        setConnection(dialog.connection());
    } else {
        logoutAction->setEnabled(false);
    }
}

void MainWindow::logout()
{
    connection->logout();

    QSettings settings;
    settings.beginGroup("Login");
    settings.remove("userid");
    settings.remove("token");
    settings.remove("savecredentials");
    settings.endGroup();
    settings.sync();
}

void MainWindow::loggedOut()
{
    loginAction->setEnabled(true);
    logoutAction->setEnabled(false);
    setConnection(nullptr);
    // This posts an event to the event queue and lets the logout to complete
    loginAction->trigger();
}

void MainWindow::getNewEvents()
{
    //qDebug() << "getNewEvents";
    connection->sync(30*1000);
}

void MainWindow::gotEvents()
{
    // qDebug() << "newEvents";
    getNewEvents();
}

void MainWindow::connectionError(QString error)
{
    qDebug() << error;
    qDebug() << "reconnecting...";
    connection->reconnect();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    setConnection(nullptr);

    event->accept();
}

void MainWindow::showJoinRoomDialog()
{
    bool ok;
    QString room = QInputDialog::getText(this, tr("Join Room"), tr("Enter the name of the room"),
                                         QLineEdit::Normal, QString(), &ok);
    if( ok && !room.isEmpty() )
    {
        connection->joinRoom(room);
    }
}


