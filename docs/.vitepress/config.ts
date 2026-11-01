import { defineConfig } from "vitepress";

export default defineConfig({
  title: "OpenSheep",
  description: "A faithful reimplementation of Sheep (2000), using the data from your own CD.",
  cleanUrls: true,
  lastUpdated: true,
  srcExclude: ["README.md", "AGENTS.md"],
  sitemap: { hostname: "https://opensheep.emdzej.pl" },
  // The browser player is a static page in public/play/, not a Markdown page.
  ignoreDeadLinks: [/^\/play\//],

  head: [
    ["meta", { name: "theme-color", content: "#185108" }],
    ["meta", { property: "og:title", content: "OpenSheep — Sheep, native again" }],
    ["meta", { property: "og:description", content: "A faithful, from-scratch reimplementation of Sheep, on the gasm runtime: macOS, Linux, Windows and the browser." }],
    ["meta", { property: "og:url", content: "https://opensheep.emdzej.pl/" }],
  ],

  themeConfig: {
    siteTitle: "OpenSheep",

    nav: [
      { text: "User guide", link: "/guide/", activeMatch: "/guide/" },
      { text: "Internals", link: "/architecture", activeMatch: "/(architecture|formats)" },
      // A static app in docs/public/play/: target _self makes it a full page load, not a VitePress route.
      { text: "Play", link: "/play/", target: "_self" },
    ],

    sidebar: {
      "/guide/": [
        {
          text: "User guide",
          items: [
            { text: "Introduction", link: "/guide/" },
            { text: "Installing", link: "/guide/install" },
            { text: "Game data", link: "/guide/game-data" },
            { text: "Playing", link: "/guide/playing" },
            { text: "Controls", link: "/guide/controls" },
            { text: "Developer options", link: "/guide/developer" },
            { text: "Building", link: "/guide/build" },
          ],
        },
      ],
      "/": [
        {
          text: "Internals",
          items: [
            { text: "Engine architecture", link: "/architecture" },
            { text: "File formats", link: "/formats" },
          ],
        },
      ],
    },

    socialLinks: [{ icon: "github", link: "https://github.com/emdzej/opensheep" }],

    editLink: {
      pattern: "https://github.com/emdzej/opensheep/edit/main/docs/:path",
      text: "Edit this page on GitHub",
    },

    search: { provider: "local" },

    footer: {
      message:
        "OpenSheep is released under the GPL-3.0. Sheep is © 2000 Mind's Eye Productions / Empire Interactive. OpenSheep contains no original code or assets. It runs on <a href=\"https://gasm.emdzej.pl\">gasm</a>.",
    },
  },
});
