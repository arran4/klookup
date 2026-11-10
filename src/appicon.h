#ifndef APPICON_H
#define APPICON_H
#include <QIcon>
class QPalette;
QIcon applicationIcon();
QIcon trayIconForPalette(const QPalette &palette);
#endif
