/****************************************************************************
** Meta object code from reading C++ file 'explorer.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../explorer.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'explorer.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_Explorer[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: signature, parameters, type, tag, flags
      18,   10,    9,    9, 0x05,

 // slots: signature, parameters, type, tag, flags
      64,   47,    9,    9, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_Explorer[] = {
    "Explorer\0\0current\0currentSelected(QModelIndex)\0"
    "current,previous\0"
    "currentChanged(QModelIndex,QModelIndex)\0"
};

void Explorer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        Explorer *_t = static_cast<Explorer *>(_o);
        switch (_id) {
        case 0: _t->currentSelected((*reinterpret_cast< const QModelIndex(*)>(_a[1]))); break;
        case 1: _t->currentChanged((*reinterpret_cast< const QModelIndex(*)>(_a[1])),(*reinterpret_cast< const QModelIndex(*)>(_a[2]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData Explorer::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject Explorer::staticMetaObject = {
    { &QTreeView::staticMetaObject, qt_meta_stringdata_Explorer,
      qt_meta_data_Explorer, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &Explorer::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *Explorer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *Explorer::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_Explorer))
        return static_cast<void*>(const_cast< Explorer*>(this));
    return QTreeView::qt_metacast(_clname);
}

int Explorer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QTreeView::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void Explorer::currentSelected(const QModelIndex & _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_END_MOC_NAMESPACE
