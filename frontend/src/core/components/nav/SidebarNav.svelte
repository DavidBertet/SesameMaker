<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import * as Sidebar from '$lib/components/ui/sidebar'
  import { cn } from '$lib/utils'

  let { tabs, localTabs = [], activeTab, onTabSelect } = $props()

  let sidebar = Sidebar.useSidebar()

  function setActiveTab(tab) {
    sidebar.setOpenMobile(false)
    onTabSelect(tab)
  }
</script>

{#snippet menuItems(items)}
  {#each items as tab (tab.id)}
    <Sidebar.MenuItem onclick={() => setActiveTab(tab.id)}>
      <Sidebar.MenuButton
        isActive={tab.id == activeTab}
        class="w-full flex items-center gap-3 px-3 py-5 rounded-lg text-sm font-medium transition-all duration-200 cursor-pointer hover:bg-accent/50 data-[active=true]:bg-primary data-[active=true]:text-primary-foreground data-[active=true]:shadow-md data-[active=true]:shadow-primary/20"
      >
        <tab.icon
          size={18}
          class={cn(
            'shrink-0 transition-colors duration-200',
            tab.id === activeTab ? 'text-primary-foreground' : 'text-muted-foreground',
          )}
        />
        <span
          class={cn(
            'transition-colors duration-200',
            tab.id === activeTab ? 'text-primary-foreground' : 'text-foreground',
          )}
        >
          {tab.label}
        </span>
      </Sidebar.MenuButton>
    </Sidebar.MenuItem>
  {/each}
{/snippet}

<Sidebar.Group class="px-3 py-2">
  <Sidebar.GroupContent>
    <Sidebar.Menu class="space-y-1">
      {@render menuItems(tabs)}
    </Sidebar.Menu>
    {#if localTabs.length}
      <div class="mt-2 rounded-xl border border-blue-500/30 bg-blue-500/10 px-1 py-1">
        <Sidebar.GroupLabel class="text-blue-600 dark:text-blue-400"
          >Getting started</Sidebar.GroupLabel
        >
        <Sidebar.Menu class="space-y-1">
          {@render menuItems(localTabs)}
        </Sidebar.Menu>
      </div>
    {/if}
  </Sidebar.GroupContent>
</Sidebar.Group>
