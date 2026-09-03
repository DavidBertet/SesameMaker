<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->
<script>
  import { Button } from '$lib/components/ui/button'
  import { fade } from 'svelte/transition'
  import { RefreshCw } from 'lucide-svelte'

  let {
    loading = false,
    loadingLabel,
    icon = undefined,
    class: className,
    children,
    ...rest
  } = $props()

  let Icn = $derived(icon)
</script>

<Button class={className} {...rest}>
  <span class:size-4={!icon} class="inline-flex items-center justify-center">
    {#key loading}
      {#if loading}
        <span
          class="inline-flex size-4 shrink-0 items-center justify-center"
          transition:fade={{ duration: 200 }}
        >
          <RefreshCw class="size-4 animate-spin" />
        </span>
      {:else if icon}
        <span
          class="inline-flex size-4 shrink-0 items-center justify-center"
          transition:fade={{ duration: 200 }}
        >
          <Icn class="size-4" />
        </span>
      {:else}
        <span class="size-4 shrink-0"></span>
      {/if}
    {/key}
  </span>
  {#if loading && loadingLabel}
    {loadingLabel}
  {:else}
    {@render children?.()}
  {/if}
  {#if !icon}
    <span class="size-4 shrink-0" aria-hidden="true"></span>
  {/if}
</Button>
