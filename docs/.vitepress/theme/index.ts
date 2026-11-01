import { h } from "vue";
import DefaultTheme from "vitepress/theme";
import Facts from "./Facts.vue";
import Flock from "./Flock.vue";
import "./pasture.css";

export default {
  extends: DefaultTheme,
  Layout: () =>
    h(DefaultTheme.Layout, null, {
      "home-hero-image": () => h(Flock),
      "home-hero-actions-after": () => h(Facts),
    }),
};
