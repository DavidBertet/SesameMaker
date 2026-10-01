<script>
  import { cn } from '$lib/utils.js'

  let { ref = $bindable(null), enabled = true, class: className, children, ...restProps } = $props()
</script>

<div
	bind:this={ref}
	data-slot="card-enabled"
  class={cn(
    'space-y-4 transition-all duration-300',
    !enabled && 'pointer-events-none select-none opacity-60 blur-[2px]',
    className,
  )}
  {...restProps}
>
  {@render children?.()}
</div>
