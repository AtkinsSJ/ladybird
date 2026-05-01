/*
 * Copyright (c) 2024, Tim Ledbetter <tim.ledbetter@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Bindings/ServiceWorkerRegistration.h>
#include <LibWeb/ServiceWorker/Job.h>
#include <LibWeb/ServiceWorker/ServiceWorker.h>
#include <LibWeb/ServiceWorker/ServiceWorkerRegistration.h>
#include <LibWeb/WebIDL/Promise.h>

namespace Web::ServiceWorker {

GC_DEFINE_ALLOCATOR(ServiceWorkerRegistration);

ServiceWorkerRegistration::ServiceWorkerRegistration(JS::Realm& realm, Registration const& registration)
    : DOM::EventTarget(realm)
    , m_registration(registration)
{
}

void ServiceWorkerRegistration::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(ServiceWorkerRegistration);
    Base::initialize(realm);
}

void ServiceWorkerRegistration::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_installing);
    visitor.visit(m_waiting);
    visitor.visit(m_active);
}

GC::Ref<ServiceWorkerRegistration> ServiceWorkerRegistration::create(JS::Realm& realm, Registration const& registration)
{
    return realm.create<ServiceWorkerRegistration>(realm, registration);
}

// https://w3c.github.io/ServiceWorker/#navigator-service-worker-unregister
GC::Ref<WebIDL::Promise> ServiceWorkerRegistration::unregister()
{
    auto& realm = this->realm();

    // 1. Let registration be the service worker registration.
    auto& registration = this->registration();

    // 2. Let promise be a new promise.
    auto promise = WebIDL::create_promise(realm);

    // 3. Let job be the result of running Create Job with unregister, registration’s storage key, registration’s scope
    //    url, null, promise, and this’s relevant settings object.
    auto job = Job::create(vm(), Job::Type::Unregister, registration.storage_key(), registration.scope_url(), {}, promise, HTML::relevant_settings_object(*this));

    // 4. Invoke Schedule Job with job.
    schedule_job(vm(), job);

    // 5. Return promise.
    return promise;
}

}
