import type {Metadata} from 'next';

export const metadata: Metadata = {title: 'Signed out - Rebalancer Explorer'};

export default function Layout({children}: {children: React.ReactNode}) {
  return children;
}
