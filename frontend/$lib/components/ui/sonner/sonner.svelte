<script>
  import { onMount } from 'svelte'
  import { Toaster as Sonner } from 'svelte-sonner'
  import Loader2Icon from '@lucide/svelte/icons/loader-2'
  import CircleCheckIcon from '@lucide/svelte/icons/circle-check'
  import OctagonXIcon from '@lucide/svelte/icons/octagon-x'
  import InfoIcon from '@lucide/svelte/icons/info'
  import TriangleAlertIcon from '@lucide/svelte/icons/triangle-alert'

  let { ...restProps } = $props()

  // Follow the app's hand-rolled toggle (DarkModeToggle.svelte): it adds or
  // removes the `dark` class on <html> and persists the choice in localStorage.
  let theme = $state(
    typeof document !== 'undefined' && document.documentElement.classList.contains('dark')
      ? 'dark'
      : 'light',
  )

  function syncTheme() {
    theme = document.documentElement.classList.contains('dark') ? 'dark' : 'light'
  }

  onMount(() => {
    syncTheme()
    const observer = new MutationObserver(syncTheme)
    observer.observe(document.documentElement, { attributes: true, attributeFilter: ['class'] })
    return () => observer.disconnect()
  })
</script>

<Sonner
  {theme}
  class="toaster group"
  style="--normal-bg: var(--color-popover); --normal-text: var(--color-popover-foreground); --normal-border: var(--color-border);"
  {...restProps}
>
  {#snippet loadingIcon()}
    <Loader2Icon class="size-4 animate-spin" />
  {/snippet}
  {#snippet successIcon()}
    <CircleCheckIcon class="size-4" />
  {/snippet}
  {#snippet errorIcon()}
    <OctagonXIcon class="size-4" />
  {/snippet}
  {#snippet infoIcon()}
    <InfoIcon class="size-4" />
  {/snippet}
  {#snippet warningIcon()}
    <TriangleAlertIcon class="size-4" />
  {/snippet}
</Sonner>
