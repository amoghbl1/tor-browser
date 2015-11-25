// Copyright (c) 2015, The Tor Project, Inc.
// See LICENSE for licensing information.
//
// vim: set sw=2 sts=2 ts=8 et syntax=javascript:

function init()
{
  let event = new CustomEvent("AboutTBUpdateLoad", { bubbles: true });
  document.dispatchEvent(event);
}
