/*
 * Copyright (c) 2026, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/JsonArray.h>
#include <LibDevTools/Actors/NodeListActor.h>
#include <LibDevTools/Actors/WalkerActor.h>
#include <LibDevTools/DevToolsServer.h>

namespace DevTools {

NonnullRefPtr<NodeListActor> NodeListActor::create(DevToolsServer& devtools, String name, WeakPtr<WalkerActor> walker, Vector<Web::UniqueNodeID> node_ids)
{
    return adopt_ref(*new NodeListActor(devtools, move(name), move(walker), move(node_ids)));
}

NodeListActor::NodeListActor(DevToolsServer& devtools, String name, WeakPtr<WalkerActor> walker, Vector<Web::UniqueNodeID> node_ids)
    : Actor(devtools, move(name))
    , m_walker(move(walker))
    , m_node_ids(move(node_ids))
{
}

void NodeListActor::handle_message(Message const& message)
{
    JsonObject response;

    if (message.type == "item"sv) {
        auto item = get_required_parameter<size_t>(message, "item"sv);
        if (!item.has_value())
            return;

        auto walker = m_walker.strong_ref();
        if (!walker || *item >= m_node_ids.size()) {
            send_response(message, move(response));
            return;
        }

        if (auto node = walker->node_for_id(m_node_ids[*item]); node.has_value())
            response = walker->serialize_disconnected_node(*node);
        send_response(message, move(response));
        return;
    }

    if (message.type == "items"sv) {
        auto start = message.data.get_integer<size_t>("start"sv).value_or(0);
        auto end = message.data.get_integer<size_t>("end"sv).value_or(m_node_ids.size());
        start = min(start, m_node_ids.size());
        end = min(max(start, end), m_node_ids.size());

        JsonArray nodes;
        if (auto walker = m_walker.strong_ref()) {
            for (auto index = start; index < end; ++index) {
                if (auto node = walker->node_for_id(m_node_ids[index]); node.has_value())
                    nodes.must_append(walker->serialize_node(*node));
            }
        }

        response.set("nodes"sv, move(nodes));
        response.set("newParents"sv, JsonArray {});
        send_response(message, move(response));
        return;
    }

    if (message.type == "release"sv) {
        send_response(message, move(response));
        devtools().unregister_actor(name());
        return;
    }

    send_unrecognized_packet_type_error(message);
}

}
