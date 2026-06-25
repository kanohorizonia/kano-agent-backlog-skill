#pragma once

#include <string_view>

namespace kano::backlog::webview::assets {

inline constexpr std::string_view kKobUiJs = R"JS(
(function () {
  'use strict';

  const timers = new Map();

  function targetElement(target) {
    if (!target) return null;
    if (typeof target === 'string') return document.querySelector(target);
    return target;
  }

  function showError(target, message) {
    const element = targetElement(target);
    const text = String(message || 'Request failed');
    const html = '<div class="card" role="alert"><strong>Unable to load content</strong><div class="muted">' +
      text.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;') +
      '</div></div>';
    if (element) {
      element.innerHTML = html;
    }
    return html;
  }

  function swapTarget(target, html) {
    const element = targetElement(target);
    if (!element) {
      throw new Error('Partial target not found');
    }
    element.innerHTML = html;
    element.dispatchEvent(new CustomEvent('kob-ui:swapped', { bubbles: true, detail: { target: element } }));
    return element;
  }

  async function fetchPartial(url, options) {
    const settings = options || {};
    const timeoutMs = Number(settings.timeoutMs || 15000);
    const controller = new AbortController();
    const externalSignal = settings.signal;
    const timeout = setTimeout(function () {
      controller.abort();
    }, timeoutMs);

    if (externalSignal) {
      if (externalSignal.aborted) {
        controller.abort();
      } else {
        externalSignal.addEventListener('abort', function () {
          controller.abort();
        }, { once: true });
      }
    }

    try {
      if (settings.onStatus) settings.onStatus('loading');
      const response = await fetch(url, {
        method: settings.method || 'GET',
        signal: controller.signal,
        headers: { 'Accept': 'text/html' },
      });
      const html = await response.text();
      if (!response.ok) {
        throw new Error('HTTP ' + response.status);
      }
      if (settings.target) {
        swapTarget(settings.target, html);
      }
      if (settings.onStatus) settings.onStatus('loaded');
      return html;
    } catch (error) {
      if (settings.target) {
        showError(settings.target, error && error.message ? error.message : String(error));
      }
      if (settings.onStatus) settings.onStatus('failed');
      throw error;
    } finally {
      clearTimeout(timeout);
    }
  }

  function debounce(key, run, delayMs) {
    const name = String(key || 'default');
    if (timers.has(name)) {
      clearTimeout(timers.get(name));
    }
    const timer = setTimeout(function () {
      timers.delete(name);
      run();
    }, Number(delayMs || 0));
    timers.set(name, timer);
    return timer;
  }

  function cancelDebounce(key) {
    const name = String(key || 'default');
    if (!timers.has(name)) return;
    clearTimeout(timers.get(name));
    timers.delete(name);
  }

  function setQueryState(update, options) {
    const settings = options || {};
    const url = new URL(window.location.href);
    Object.entries(update || {}).forEach(function (entry) {
      const key = entry[0];
      const value = entry[1];
      if (value === null || value === undefined || value === '') {
        url.searchParams.delete(key);
      } else {
        url.searchParams.set(key, String(value));
      }
    });
    if (settings.replace !== false) {
      window.history.replaceState({}, '', url);
    } else {
      window.history.pushState({}, '', url);
    }
  }

  function readQueryState() {
    const result = {};
    new URLSearchParams(window.location.search).forEach(function (value, key) {
      result[key] = value;
    });
    return result;
  }

  function bindDelegates(root) {
    const container = root || document;
    if (container.__kobUiDelegatesBound) return;
    container.__kobUiDelegatesBound = true;
    container.addEventListener('click', function (event) {
      const trigger = event.target && event.target.closest
        ? event.target.closest('[data-kob-partial]')
        : null;
      if (!trigger) return;
      event.preventDefault();
      const url = trigger.getAttribute('data-kob-partial');
      const target = trigger.getAttribute('data-kob-target');
      if (!url || !target) return;
      fetchPartial(url, { target: target }).catch(function () {});
    });
  }

  window.KobUi = {
    bindDelegates: bindDelegates,
    cancelDebounce: cancelDebounce,
    debounce: debounce,
    fetchPartial: fetchPartial,
    readQueryState: readQueryState,
    setQueryState: setQueryState,
    showError: showError,
    swapTarget: swapTarget,
  };

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function () { bindDelegates(document); });
  } else {
    bindDelegates(document);
  }
})();
)JS";

inline constexpr std::string_view KobUiJs() noexcept {
  return kKobUiJs;
}

}  // namespace kano::backlog::webview::assets
