import { Command as CommandPrimitive } from 'bits-ui'

import Group from './command-group.svelte'
import Input from './command-input.svelte'
import Item from './command-item.svelte'
import List from './command-list.svelte'
import Empty from './command-empty.svelte'

const Root = CommandPrimitive.Root

export {
  Root,
  Group,
  Input,
  Item,
  List,
  Empty,
  //
  Root as Command,
  Group as CommandGroup,
  Input as CommandInput,
  Item as CommandItem,
  List as CommandList,
  Empty as CommandEmpty,
}
