/****************************************************************************
** Meta object code from reading C++ file 'scheduleview.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../scheduleview.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'scheduleview.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_ScheduleView[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
       3,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: signature, parameters, type, tag, flags
      20,   14,   13,   13, 0x05,
      49,   14,   13,   13, 0x05,

 // slots: signature, parameters, type, tag, flags
      84,   79,   13,   13, 0x0a,

       0        // eod
};

static const char qt_meta_stringdata_ScheduleView[] = {
    "ScheduleView\0\0value\0taskScrollChangedSignal(int)\0"
    "chartScrollChangedSignal(int)\0item\0"
    "itemChangedSlot(QTreeWidgetItem*)\0"
};

void ScheduleView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        ScheduleView *_t = static_cast<ScheduleView *>(_o);
        switch (_id) {
        case 0: _t->taskScrollChangedSignal((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 1: _t->chartScrollChangedSignal((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 2: _t->itemChangedSlot((*reinterpret_cast< QTreeWidgetItem*(*)>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObjectExtraData ScheduleView::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject ScheduleView::staticMetaObject = {
    { &QWidget::staticMetaObject, qt_meta_stringdata_ScheduleView,
      qt_meta_data_ScheduleView, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &ScheduleView::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *ScheduleView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *ScheduleView::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_ScheduleView))
        return static_cast<void*>(const_cast< ScheduleView*>(this));
    return QWidget::qt_metacast(_clname);
}

int ScheduleView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 3)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 3;
    }
    return _id;
}

// SIGNAL 0
void ScheduleView::taskScrollChangedSignal(int _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void ScheduleView::chartScrollChangedSignal(int _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}
QT_END_MOC_NAMESPACE
