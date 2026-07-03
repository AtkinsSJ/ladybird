/*
 * Copyright (c) 2026, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/String.h>
#include <AK/Vector.h>
#include <LibDevTools/Actor.h>
#include <LibDevTools/Forward.h>
#include <LibWeb/Forward.h>

namespace DevTools {

class NodeListActor final : public Actor {
public:
    static constexpr auto base_name = "dom-node-list"sv;

    static NonnullRefPtr<NodeListActor> create(DevToolsServer&, String name, WeakPtr<WalkerActor>, Vector<Web::UniqueNodeID>);

private:
    NodeListActor(DevToolsServer&, String name, WeakPtr<WalkerActor>, Vector<Web::UniqueNodeID>);

    virtual void handle_message(Message const&) override;

    WeakPtr<WalkerActor> m_walker;
    Vector<Web::UniqueNodeID> m_node_ids;
};

}
