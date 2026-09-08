// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Settings module. All the logic is in LostPhoneBackend, shared with the
// standalone application; this only wires it to the KCM frame so the two
// front-ends cannot drift apart.

#include "backend.h"

#include <KPluginFactory>
#include <KQuickConfigModule>

class KCMLostPhone : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(LostPhoneBackend *backend READ backend CONSTANT)

public:
    KCMLostPhone(QObject *parent, const KPluginMetaData &data)
        : KQuickConfigModule(parent, data)
        , m_backend(new LostPhoneBackend(this))
    {
        connect(m_backend, &LostPhoneBackend::dirtyChanged, this, [this] {
            setNeedsSave(m_backend->dirty());
        });
        setNeedsSave(m_backend->dirty());
    }

    LostPhoneBackend *backend() const { return m_backend; }

    void load() override { m_backend->load(); }
    void save() override { m_backend->save(); }
    void defaults() override { m_backend->restoreDefaults(); }

private:
    LostPhoneBackend *const m_backend;
};

K_PLUGIN_CLASS_WITH_JSON(KCMLostPhone, "kcm_lost_phone.json")

#include "kcm.moc"
