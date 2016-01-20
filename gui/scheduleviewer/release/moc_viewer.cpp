/****************************************************************************
** Meta object code from reading C++ file 'viewer.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../viewer.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'viewer.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_Viewer[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
       8,    7,    7,    7, 0x08,
      24,   15,    7,    7, 0x08,
      38,    7,    7,    7, 0x08,
      53,   15,    7,    7, 0x08,
      75,    7,    7,    7, 0x08,
      93,   82,    7,    7, 0x08,
     121,   82,    7,    7, 0x08,
     149,   82,    7,    7, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_Viewer[] = {
    "Viewer\0\0open()\0filename\0open(QString)\0"
    "openPipeline()\0openPipeline(QString)\0"
    "quit()\0modelIndex\0currentClicked(QModelIndex)\0"
    "currentChanged(QModelIndex)\0"
    "updateView(QModelIndex)\0"
};

void Viewer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        Viewer *_t = static_cast<Viewer *>(_o);
        switch (_id) {
        case 0: _t->open(); break;
        case 1: _t->open((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->openPipeline(); break;
        case 3: _t->openPipeline((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 4: _t->quit(); break;
        case 5: _t->currentClicked((*reinterpret_cast< QModelIndex(*)>(_a[1]))); break;
        case 6: _t->currentChanged((*reinterpret_cast< QModelIndex(*)>(_a[1]))); break;
        case 7: _t->updateView((*reinterpret_cast< QModelIndex(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData Viewer::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject Viewer::staticMetaObject = {
    { &QMainWindow::staticMetaObject, qt_meta_stringdata_Viewer,
      qt_meta_data_Viewer, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &Viewer::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *Viewer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *Viewer::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_Viewer))
        return static_cast<void*>(const_cast< Viewer*>(this));
    return QMainWindow::qt_metacast(_clname);
}

int Viewer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
