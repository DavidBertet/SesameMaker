<script>
  import { Command as CommandPrimitive } from 'bits-ui'
  import { cn } from '$lib/utils.js'

  let { ref = $bindable(null), class: className, value, children, ...restProps } = $props()
</script>

<CommandPrimitive.Group
  bind:ref
  data-slot="command-group"
  class={cn('text-foreground overflow-hidden p-1', className)}
  {value}
  {...restProps}
>
  {@render children?.()}
</CommandPrimitive.Group>
