/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, Sam Atkins <atkinssj@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ScopeGuard.h>
#include <AK/SourceLocation.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/BrowsingContext.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/HTML/HTMLVideoElement.h>
#include <LibWeb/HTML/Scripting/Environments.h>
#include <LibWeb/Page/Page.h>
#include <LibWeb/Platform/EventLoopPlugin.h>

namespace Web {

Page::Page(PageClient& client)
    : m_client(client)
{
    m_top_level_browsing_context = JS::make_handle(*HTML::BrowsingContext::create_a_new_top_level_browsing_context(*this));
}

Page::~Page() = default;

HTML::BrowsingContext& Page::focused_context()
{
    if (m_focused_context)
        return *m_focused_context;
    return top_level_browsing_context();
}

void Page::set_focused_browsing_context(Badge<EventHandler>, HTML::BrowsingContext& browsing_context)
{
    m_focused_context = browsing_context.make_weak_ptr();
}

void Page::load(const AK::URL& url)
{
    top_level_browsing_context().loader().load(url, FrameLoader::Type::Navigation);
}

void Page::load(LoadRequest& request)
{
    top_level_browsing_context().loader().load(request, FrameLoader::Type::Navigation);
}

void Page::load_html(StringView html, const AK::URL& url)
{
    top_level_browsing_context().loader().load_html(html, url);
}

bool Page::has_ongoing_navigation() const
{
    return top_level_browsing_context().loader().is_pending();
}

Gfx::Palette Page::palette() const
{
    return m_client.palette();
}

// https://w3c.github.io/csswg-drafts/cssom-view-1/#web-exposed-screen-area
CSSPixelRect Page::web_exposed_screen_area() const
{
    auto device_pixel_rect = m_client.screen_rect();
    auto scale = client().device_pixels_per_css_pixel();
    return {
        device_pixel_rect.x().value() / scale,
        device_pixel_rect.y().value() / scale,
        device_pixel_rect.width().value() / scale,
        device_pixel_rect.height().value() / scale
    };
}

CSS::PreferredColorScheme Page::preferred_color_scheme() const
{
    return m_client.preferred_color_scheme();
}

CSSPixelPoint Page::device_to_css_point(DevicePixelPoint point) const
{
    return {
        point.x().value() / client().device_pixels_per_css_pixel(),
        point.y().value() / client().device_pixels_per_css_pixel(),
    };
}

DevicePixelPoint Page::css_to_device_point(CSSPixelPoint point) const
{
    return {
        point.x().value() * client().device_pixels_per_css_pixel(),
        point.y().value() * client().device_pixels_per_css_pixel(),
    };
}

CSSPixelRect Page::device_to_css_rect(DevicePixelRect rect) const
{
    auto scale = client().device_pixels_per_css_pixel();
    return {
        rect.x().value() / scale,
        rect.y().value() / scale,
        rect.width().value() / scale,
        rect.height().value() / scale
    };
}

DevicePixelRect Page::enclosing_device_rect(CSSPixelRect rect) const
{
    auto scale = client().device_pixels_per_css_pixel();
    return DevicePixelRect(
        floor(rect.x().value() * scale),
        floor(rect.y().value() * scale),
        ceil(rect.width().value() * scale),
        ceil(rect.height().value() * scale));
}

DevicePixelRect Page::rounded_device_rect(CSSPixelRect rect) const
{
    auto scale = client().device_pixels_per_css_pixel();
    return {
        roundf(rect.x().value() * scale),
        roundf(rect.y().value() * scale),
        roundf(rect.width().value() * scale),
        roundf(rect.height().value() * scale)
    };
}

bool Page::handle_mousewheel(DevicePixelPoint position, unsigned button, unsigned buttons, unsigned modifiers, int wheel_delta_x, int wheel_delta_y)
{
    return top_level_browsing_context().event_handler().handle_mousewheel(device_to_css_point(position), button, buttons, modifiers, wheel_delta_x, wheel_delta_y);
}

bool Page::handle_mouseup(DevicePixelPoint position, unsigned button, unsigned buttons, unsigned modifiers)
{
    return top_level_browsing_context().event_handler().handle_mouseup(device_to_css_point(position), button, buttons, modifiers);
}

bool Page::handle_mousedown(DevicePixelPoint position, unsigned button, unsigned buttons, unsigned modifiers)
{
    return top_level_browsing_context().event_handler().handle_mousedown(device_to_css_point(position), button, buttons, modifiers);
}

bool Page::handle_mousemove(DevicePixelPoint position, unsigned buttons, unsigned modifiers)
{
    return top_level_browsing_context().event_handler().handle_mousemove(device_to_css_point(position), buttons, modifiers);
}

bool Page::handle_doubleclick(DevicePixelPoint position, unsigned button, unsigned buttons, unsigned modifiers)
{
    return top_level_browsing_context().event_handler().handle_doubleclick(device_to_css_point(position), button, buttons, modifiers);
}

bool Page::handle_keydown(KeyCode key, unsigned modifiers, u32 code_point)
{
    return focused_context().event_handler().handle_keydown(key, modifiers, code_point);
}

bool Page::handle_keyup(KeyCode key, unsigned modifiers, u32 code_point)
{
    return focused_context().event_handler().handle_keyup(key, modifiers, code_point);
}

bool Page::top_level_browsing_context_is_initialized() const
{
    return m_top_level_browsing_context;
}

HTML::BrowsingContext& Page::top_level_browsing_context()
{
    return *m_top_level_browsing_context;
}

HTML::BrowsingContext const& Page::top_level_browsing_context() const
{
    return *m_top_level_browsing_context;
}

template<typename ResponseType>
static ResponseType spin_event_loop_until_dialog_closed(PageClient& client, Optional<ResponseType>& response, SourceLocation location = SourceLocation::current())
{
    auto& event_loop = Web::HTML::current_settings_object().responsible_event_loop();

    ScopeGuard guard { [&] { event_loop.set_execution_paused(false); } };
    event_loop.set_execution_paused(true);

    Web::Platform::EventLoopPlugin::the().spin_until([&]() {
        return response.has_value() || !client.is_connection_open();
    });

    if (!client.is_connection_open()) {
        dbgln("WebContent client disconnected during {}. Exiting peacefully.", location.function_name());
        exit(0);
    }

    return response.release_value();
}

void Page::did_request_alert(String const& message)
{
    m_pending_dialog = PendingDialog::Alert;
    m_client.page_did_request_alert(message);

    if (!message.is_empty())
        m_pending_dialog_text = message;

    spin_event_loop_until_dialog_closed(m_client, m_pending_alert_response);
}

void Page::alert_closed()
{
    if (m_pending_dialog == PendingDialog::Alert) {
        m_pending_dialog = PendingDialog::None;
        m_pending_alert_response = Empty {};
        m_pending_dialog_text.clear();
    }
}

bool Page::did_request_confirm(String const& message)
{
    m_pending_dialog = PendingDialog::Confirm;
    m_client.page_did_request_confirm(message);

    if (!message.is_empty())
        m_pending_dialog_text = message;

    return spin_event_loop_until_dialog_closed(m_client, m_pending_confirm_response);
}

void Page::confirm_closed(bool accepted)
{
    if (m_pending_dialog == PendingDialog::Confirm) {
        m_pending_dialog = PendingDialog::None;
        m_pending_confirm_response = accepted;
        m_pending_dialog_text.clear();
    }
}

Optional<String> Page::did_request_prompt(String const& message, String const& default_)
{
    m_pending_dialog = PendingDialog::Prompt;
    m_client.page_did_request_prompt(message, default_);

    if (!message.is_empty())
        m_pending_dialog_text = message;

    return spin_event_loop_until_dialog_closed(m_client, m_pending_prompt_response);
}

void Page::prompt_closed(Optional<String> response)
{
    if (m_pending_dialog == PendingDialog::Prompt) {
        m_pending_dialog = PendingDialog::None;
        m_pending_prompt_response = move(response);
        m_pending_dialog_text.clear();
    }
}

void Page::dismiss_dialog()
{
    switch (m_pending_dialog) {
    case PendingDialog::None:
        break;
    case PendingDialog::Alert:
        m_client.page_did_request_accept_dialog();
        break;
    case PendingDialog::Confirm:
    case PendingDialog::Prompt:
        m_client.page_did_request_dismiss_dialog();
        break;
    }
}

void Page::accept_dialog()
{
    switch (m_pending_dialog) {
    case PendingDialog::None:
        break;
    case PendingDialog::Alert:
    case PendingDialog::Confirm:
    case PendingDialog::Prompt:
        m_client.page_did_request_accept_dialog();
        break;
    }
}

void Page::did_request_video_context_menu(i32 video_id, CSSPixelPoint position, AK::URL const& url, DeprecatedString const& target, unsigned modifiers, bool is_playing, bool has_user_agent_controls, bool is_looping)
{
    m_video_context_menu_element_id = video_id;
    client().page_did_request_video_context_menu(position, url, target, modifiers, is_playing, has_user_agent_controls, is_looping);
}

WebIDL::ExceptionOr<void> Page::toggle_video_play_state()
{
    auto video_element = video_context_menu_element();
    if (!video_element)
        return {};

    // FIXME: This runs from outside the context of any user script, so we do not have a running execution
    //        context. This pushes one to allow the promise creation hook to run.
    auto& environment_settings = video_element->document().relevant_settings_object();
    environment_settings.prepare_to_run_script();

    ScopeGuard guard { [&] { environment_settings.clean_up_after_running_script(); } };

    if (video_element->potentially_playing())
        TRY(video_element->pause());
    else
        TRY(video_element->play());

    return {};
}

WebIDL::ExceptionOr<void> Page::toggle_video_loop_state()
{
    auto video_element = video_context_menu_element();
    if (!video_element)
        return {};

    // FIXME: This runs from outside the context of any user script, so we do not have a running execution
    //        context. This pushes one to allow the promise creation hook to run.
    auto& environment_settings = video_element->document().relevant_settings_object();
    environment_settings.prepare_to_run_script();

    ScopeGuard guard { [&] { environment_settings.clean_up_after_running_script(); } };

    if (video_element->has_attribute(HTML::AttributeNames::loop))
        video_element->remove_attribute(HTML::AttributeNames::loop);
    else
        TRY(video_element->set_attribute(HTML::AttributeNames::loop, {}));

    return {};
}

WebIDL::ExceptionOr<void> Page::toggle_video_controls_state()
{
    auto video_element = video_context_menu_element();
    if (!video_element)
        return {};

    // FIXME: This runs from outside the context of any user script, so we do not have a running execution
    //        context. This pushes one to allow the promise creation hook to run.
    auto& environment_settings = video_element->document().relevant_settings_object();
    environment_settings.prepare_to_run_script();

    ScopeGuard guard { [&] { environment_settings.clean_up_after_running_script(); } };

    if (video_element->has_attribute(HTML::AttributeNames::controls))
        video_element->remove_attribute(HTML::AttributeNames::controls);
    else
        TRY(video_element->set_attribute(HTML::AttributeNames::controls, {}));

    return {};
}

JS::GCPtr<HTML::HTMLVideoElement> Page::video_context_menu_element()
{
    if (!m_video_context_menu_element_id.has_value())
        return nullptr;

    auto* dom_node = DOM::Node::from_id(*m_video_context_menu_element_id);
    if (dom_node == nullptr)
        return nullptr;

    if (!is<HTML::HTMLVideoElement>(dom_node))
        return nullptr;

    return static_cast<HTML::HTMLVideoElement*>(dom_node);
}

}
