<script>
  import SearchIcon from '@lucide/svelte/icons/search'
  import { Command as CommandPrimitive } from 'bits-ui'
  import { cn } from '$lib/utils.js'

  let { ref = $bindable(null), class: className, value = $bindable(''), ...restProps } = $props()
</script>

<div data-slot="command-input-wrapper" class="flex h-9 items-center gap-2 border-b px-3">
  <SearchIcon class="size-4 shrink-0 opacity-50" />
  <CommandPrimitive.Input
    bind:ref
    data-slot="command-input"
    class={cn(
      'placeholder:text-muted-foreground flex h-10 w-full rounded-md bg-transparent py-3 text-sm outline-hidden disabled:cursor-not-allowed disabled:opacity-50',
      className,
    )}
    bind:value
    {...restProps}
  />
</div>
