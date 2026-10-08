<template>
  <nav class="navbar navbar-sunshine">
    <div class="container-fluid">
      <span class="navbar-brand" :title="displayName">
        <img src="/images/logo-sunshine-45.png" height="45" :alt="displayName">
        <span class="ms-2">{{ displayName }}</span>
      </span>
      <ThemeToggle/>
    </div>
  </nav>
</template>

<script>
import ThemeToggle from './ThemeToggle.vue'

export default {
  data() {
    return { displayName: __SUNSHINE_DISPLAY_NAME__ }
  },
  components: {
    ThemeToggle,
  },
}
</script>
