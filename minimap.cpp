#include "minimap.h"

#include <QPainter>
#include <QTimer>
#include <QResizeEvent>
#include <QDir>
#include <QCoreApplication>

#include <cmath>
#include <string>
#include <ctime>

#include "utils/pointer.h"
#include "utils/helpers.h"

float CANVAS_WIDTH = 1500.0f;
const float CANVAS_HEIGHT = 1200.0f;

std::string random_string(const int len) {
    static const char chars[] =
//        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";

    std::string s;
    s.reserve(len);

    srand(time(NULL));

    for (int i = 0; i < len; ++i) {
        s.push_back(chars[rand() % (sizeof(chars) - 1)]);
    }

    s.push_back(0);
    return s;
}

Minimap::Minimap(QWidget *parent) :
    QWidget(parent, Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowStaysOnTopHint),
    d3Window(NULL),
    draw_minimap(false),
    size_changed(false)
{
    setAttribute(Qt::WA_TranslucentBackground);

    setWindowTitle(random_string(5).data());

    minimapTransform.translate(CANVAS_WIDTH / 2, CANVAS_HEIGHT / 2);
    minimapTransform.rotate(-45.0);
    minimapTransform.scale(-1.0, 1.0);

    QDir dir(QCoreApplication::applicationDirPath());
    dir.mkdir("cache");

    engine = new D3::Engine();
    autoLoot = new D3::AutoLoot(engine);

    registerHotKeys();

    showMaximized();

    QTimer *t = new QTimer(this); t->setTimerType(Qt::PreciseTimer);
    connect(t, SIGNAL(timeout()), this, SLOT(update()));
    t->start(50);

    t = new QTimer(this); t->setTimerType(Qt::PreciseTimer);
    connect(t, SIGNAL(timeout()), this, SLOT(repositionWindow()));
    t->start(500);

    wchar_t str[] = L"xxx.exe";
    encode_string(str, wcslen(str) + 1);
    qDebug("%ls", decode_string(str, wcslen(str) + 1).data());
}

Minimap::~Minimap()
{
    delete autoLoot;
    delete engine;

    if (d3Window) {
        CloseHandle(d3Window);
    }

    for (int id: hotkey_ids) {
        UnregisterHotKey((HWND)winId(), id);
    }
}

void Minimap::paintEvent(QPaintEvent *)
{
    if (!d3Window || d3Window != GetForegroundWindow()) {
        return;
    }

    if (engine->update()) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        drawInfo(&p);
        drawMinimap(&p);
    }
}

void Minimap::resizeEvent(QResizeEvent *e)
{
    auto s = this->size();

    CANVAS_WIDTH = ((float)s.width())/s.height()*CANVAS_HEIGHT;

    minimapTransform.reset();
    minimapTransform.scale(s.width()/CANVAS_WIDTH, s.height()/CANVAS_HEIGHT);
    minimapTransform.translate(CANVAS_WIDTH / 2.0, CANVAS_HEIGHT / 2.0);
    minimapTransform.rotate(-45.0);
    minimapTransform.scale(-1.0, 1.0);

    QWidget::resizeEvent(e);
}

bool Minimap::nativeEvent(const QByteArray &/*eventType*/, void *message, long */*result*/)
{
    MSG* m = (MSG*)message;
    if (m->message == WM_HOTKEY) {
        if (HIWORD(m->lParam) == VK_F1) {
            draw_minimap = !draw_minimap;
        }
        else if (HIWORD(m->lParam) == VK_F2) {
            autoLoot->loot();
        }
        else if (HIWORD(m->lParam) == VK_END && LOWORD(m->lParam) == MOD_CONTROL) {
            this->close();
        }
    }

    return false;
}

void Minimap::drawInfo(QPainter *p)
{
    p->setPen(QColor(255, 255, 0));
    p->setFont(QFont("Arial", 12));

    // NOTE:offset
    p->drawText(QRectF(0, 0, 0.1*p->window().width(), 0.2*p->window().height()), Qt::AlignCenter,
               QString::asprintf("FrameCnt:%u\nAppLoopCnt:%u\nWorldSnoId:%u\nX:%.4f\nY:%.4f\nZ:%.4f\nSceneCnt:%u",
                                 Pointer<uint>()(D3::Addr_ObjectManager, offsetof(D3::ObjectManager,x038_Counter_CurrentFrame)),
                                 engine->ApplicationLoopCount,
                                 engine->localData.x0C_WorldSnoId,
                                 engine->localData.x24_WorldPosX,
                                 engine->localData.x28_WorldPosY,
                                 engine->localData.x2C_WorldPosZ,
                                 engine->navMesh->sceneData.size()));
}

void Minimap::drawMinimap(QPainter *p)
{
    if (!draw_minimap || !engine->isInGame()) {
        return;
    }

    p->save();

    p->setTransform(minimapTransform);

    p->save();
//    drawCoordinates(p);

    p->translate(-engine->localData.x24_WorldPosX, -engine->localData.x28_WorldPosY);

    QVector<QRectF> scene_cells, scene_grids;
    scene_cells.reserve(100*engine->navMesh->sceneData.size());

    for (const auto& s : engine->navMesh->sceneData) {
        scene_grids.push_back(QRectF(s->min.x, s->min.y, s->max.x-s->min.x, s->max.y-s->min.y));

        auto ss = s->findSceneSnoData();
        if (!ss) {
            continue;
        }

        for (const D3::NavCell &c: ss->cells) {
            QRectF r_cell(c.min.x+s->min.x, c.min.y+s->min.y, c.max.x-c.min.x, c.max.y-c.min.y);
            scene_cells.push_back(r_cell);
        }
    }

//    p->setPen(QColor(0, 0, 255));
//    p->setBrush(QColor(0, 0, 255, 64));
//    p->drawRects(scene_grids);

    p->setPen(Qt::transparent);
    p->setBrush(QColor(0, 255, 0, 64));
    p->drawRects(scene_cells);

    int radius = 3;
    for (const D3::ActorCommonData& acd : engine->acds) {
        if (acd.x000_Id == D3::INVALID_ID) {
            continue;
        }

        if (D3::isMonster(acd)) {
            switch ((D3::MonsterQuality)acd.x0B8_MonsterQuality) {
            case D3::MonsterQuality::Hireling : {
                continue;
            }
            case D3::MonsterQuality::Normal :
            case D3::MonsterQuality::Minion : {
                if (D3::isTreasureGoblin(acd)) {
                    radius = 5;
                    p->setPen(Qt::transparent);
                    p->setBrush(QColor(255, 255, 0, 196));
                }
                else {
                    radius = 3;
                    p->setPen(Qt::transparent);
                    p->setBrush(QColor(255, 255, 255, 64));
                }
                break;
            }
            case D3::MonsterQuality::Champion :
            case D3::MonsterQuality::Unique :
            case D3::MonsterQuality::Rare : {
                radius = 5;
                p->setPen(Qt::transparent);
                p->setBrush(QColor(255, 0, 128, 196));
                break;
            }
            case D3::MonsterQuality::Boss : {
                radius = 6;
                p->setPen(Qt::transparent);
                p->setBrush(QColor(255, 0, 0, 196));
                break;
            }
            }

            p->drawEllipse(QPointF(acd.x0D0_WorldPosX, acd.x0D4_WorldPosY), radius, radius);
        }

        if (acd.x184_ActorType == int(D3::ActorType::Gizmo)) {
            if (D3::isShrine(acd)) {
                p->setPen(QPen(QColor(255, 255, 0, 196), 3));
                p->setBrush(Qt::transparent);
                p->drawRect(QRectF(acd.x0D0_WorldPosX - 4, acd.x0D4_WorldPosY - 4, 8, 8));
            }
            else if (D3::isPoolOfReflection(acd)) {
                p->setPen(QPen(QColor(0, 0, 255, 196), 3));
                p->setBrush(Qt::transparent);
                p->drawRect(QRectF(acd.x0D0_WorldPosX - 4, acd.x0D4_WorldPosY - 4, 8, 8));
            }
        }
    }

    p->restore();

    // self
    p->setBrush(QColor(0, 0, 255, 196));
    p->drawEllipse(QPoint(0, 0), 7, 7);

    p->restore();
}

void Minimap::drawCoordinates(QPainter *p)
{
    p->setPen(Qt::red);
    p->drawLine(0, 0, 150, 0);

    p->setPen(Qt::blue);
    p->drawLine(0, 0, 0, 150);
}

void Minimap::repositionWindow()
{
    auto r = getD3ClientRect();
    if (r.isNull() || r == this->geometry()) {
        return;
    }
    else {
        this->setGeometry(r);
    }
}

QRect Minimap::getD3ClientRect()
{
    if (!d3Window && MemoryReader::instance()->getProcessId() != 0) {
        d3Window = FindMainWindow(MemoryReader::instance()->getProcessId());
    }

    if (!d3Window) {
        return QRect();
    }

    RECT r;

    if (!GetClientRect(d3Window, &r)) {
        return QRect();
    }

    ClientToScreen(d3Window, reinterpret_cast<POINT*>(&r.left));
    ClientToScreen(d3Window, reinterpret_cast<POINT*>(&r.right));

    return QRect(QPoint(r.left, r.top), QPoint(r.right, r.bottom));
}

bool Minimap::registerHotkey(const char* name, unsigned int key, unsigned int mods) {
    int id = GlobalAddAtomA(name);
    if (!RegisterHotKey((HWND)winId(), id, mods, key)) {
        qDebug("Failed to register hotkey: %s", name);
        return false;
    }
    else {
        hotkey_ids.push_back(id);
        return true;
    }
}

bool Minimap::registerHotKeys()
{
    bool result = true;

    result = registerHotkey("DEMH_mm", VK_F1);
    result = registerHotkey("DEMH_al", VK_F2);
    result = registerHotkey("DEMH_quit", VK_END, MOD_CONTROL);

    return result;
}
