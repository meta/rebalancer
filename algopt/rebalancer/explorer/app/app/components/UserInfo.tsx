'use client';

import {useState} from 'react';
import {useAuth} from '@platform/auth';
import {
  Avatar,
  Box,
  ButtonBase,
  Divider,
  ListItemIcon,
  ListItemText,
  Menu,
  MenuItem,
  Skeleton,
  Tooltip,
  Typography,
} from '@mui/material';
import {LogOut} from 'lucide-react';

import {COLOR} from '@/lib/ui-tokens';

export default function UserInfo() {
  const {user, loading, logout} = useAuth();
  const [anchorEl, setAnchorEl] = useState<null | HTMLElement>(null);
  const menuOpen = Boolean(anchorEl);

  const handleMenuOpen = (event: React.MouseEvent<HTMLElement>) => {
    setAnchorEl(event.currentTarget);
  };

  const handleMenuClose = () => {
    setAnchorEl(null);
  };

  if (loading) {
    return <Skeleton variant="circular" width={28} height={28} />;
  }

  // Don't show anything if no user
  if (!user) {
    return null;
  }

  // Generate initials from name with defensive null checks
  const initials =
    (user.name || '')
      .split(' ')
      .filter(n => n.length > 0)
      .map(n => n[0])
      .join('')
      .toUpperCase()
      .slice(0, 2) || '??';

  // Get avatar from profile picture
  const avatarUrl = user.profile_pic_uri;

  return (
    <>
      {/* The avatar; the name is in its tooltip and menu. */}
      <Tooltip title={user.name}>
        <ButtonBase
          onClick={handleMenuOpen}
          aria-haspopup="menu"
          aria-expanded={menuOpen}
          aria-label={`Account menu for ${user.name}`}
          sx={{
            borderRadius: '50%',
            flexShrink: 0,
            '&.Mui-focusVisible': {
              outline: `2px solid ${COLOR.action}`,
              outlineOffset: 2,
            },
          }}>
          <Avatar src={avatarUrl} alt={user.name}>
            {initials}
          </Avatar>
        </ButtonBase>
      </Tooltip>
      <Menu
        anchorEl={anchorEl}
        open={menuOpen}
        onClose={handleMenuClose}
        anchorOrigin={{vertical: 'bottom', horizontal: 'right'}}
        transformOrigin={{vertical: 'top', horizontal: 'right'}}
        slotProps={{paper: {sx: {width: 264}}}}>
        <Box
          sx={{
            display: 'flex',
            alignItems: 'center',
            gap: 1.5,
            px: 2,
            py: 1.5,
          }}>
          <Avatar src={avatarUrl} alt={user.name} sx={{width: 40, height: 40}}>
            {initials}
          </Avatar>
          <Box sx={{minWidth: 0}}>
            <Typography variant="subtitle2" noWrap>
              {user.name}
            </Typography>
            <Typography variant="caption" component="div" noWrap>
              @{user.username}
            </Typography>
          </Box>
        </Box>
        <Divider />
        <MenuItem
          onClick={() => {
            handleMenuClose();
            logout();
          }}>
          <ListItemIcon>
            <LogOut size={16} />
          </ListItemIcon>
          <ListItemText>Log out</ListItemText>
        </MenuItem>
      </Menu>
    </>
  );
}
