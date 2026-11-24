import type {Metadata} from 'next';

export const metadata: Metadata = {
  title: 'Rebalancer Explorer',
  description: 'Inspect and analyze Rebalancer runs',
};

export default function UnauthLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <main className="@container relative flex grow flex-col">{children}</main>
  );
}
